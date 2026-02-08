// MoonScript Runtime - Channel Support (Go-style)
// Copyright (c) 2026 greenteng.com

#include "moonrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

// ============================================================================
// Channel Structure
// ============================================================================

typedef struct ChannelItem {
    MoonValue* value;
    struct ChannelItem* next;
} ChannelItem;

typedef struct MoonChannel {
    ChannelItem* head;
    ChannelItem* tail;
    int count;
    int capacity;  // 0 = unbuffered
    bool closed;
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE not_empty;
    CONDITION_VARIABLE not_full;
#else
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
#endif
} MoonChannel;

// ============================================================================
// Channel Operations
// ============================================================================

// Create channel: chan() or chan(capacity)
MoonValue* moon_chan(MoonValue** args, int argc) {
    MoonChannel* ch = (MoonChannel*)calloc(1, sizeof(MoonChannel));
    ch->head = NULL;
    ch->tail = NULL;
    ch->count = 0;
    ch->capacity = (argc > 0) ? (int)moon_to_int(args[0]) : 0;
    ch->closed = false;
    
#ifdef _WIN32
    InitializeCriticalSection(&ch->lock);
    InitializeConditionVariable(&ch->not_empty);
    InitializeConditionVariable(&ch->not_full);
#else
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
#endif
    
    // Store channel pointer as int
    MoonValue* v = moon_int((int64_t)(uintptr_t)ch);
    v->type = MOON_DICT;  // Use dict type to mark as channel
    return v;
}

static MoonChannel* get_channel(MoonValue* val) {
    if (!val) return NULL;
    return (MoonChannel*)(uintptr_t)val->data.intVal;
}

// Send to channel: ch <- value
void moon_chan_send(MoonValue* chVal, MoonValue* value) {
    MoonChannel* ch = get_channel(chVal);
    if (!ch || ch->closed) return;
    
#ifdef _WIN32
    EnterCriticalSection(&ch->lock);
    
    // Wait if buffered channel is full
    while (ch->capacity > 0 && ch->count >= ch->capacity && !ch->closed) {
        SleepConditionVariableCS(&ch->not_full, &ch->lock, INFINITE);
    }
    
    if (ch->closed) {
        LeaveCriticalSection(&ch->lock);
        return;
    }
    
    // For unbuffered channel, wait for receiver
    if (ch->capacity == 0) {
        // Add item
        ChannelItem* item = (ChannelItem*)malloc(sizeof(ChannelItem));
        item->value = value;
        moon_retain(value);
        item->next = NULL;
        
        if (ch->tail) {
            ch->tail->next = item;
        } else {
            ch->head = item;
        }
        ch->tail = item;
        ch->count++;
        
        WakeConditionVariable(&ch->not_empty);
        
        // Wait for receiver to take it
        while (ch->count > 0 && !ch->closed) {
            SleepConditionVariableCS(&ch->not_full, &ch->lock, INFINITE);
        }
    } else {
        // Buffered: just add
        ChannelItem* item = (ChannelItem*)malloc(sizeof(ChannelItem));
        item->value = value;
        moon_retain(value);
        item->next = NULL;
        
        if (ch->tail) {
            ch->tail->next = item;
        } else {
            ch->head = item;
        }
        ch->tail = item;
        ch->count++;
        
        WakeConditionVariable(&ch->not_empty);
    }
    
    LeaveCriticalSection(&ch->lock);
#else
    pthread_mutex_lock(&ch->lock);
    
    while (ch->capacity > 0 && ch->count >= ch->capacity && !ch->closed) {
        pthread_cond_wait(&ch->not_full, &ch->lock);
    }
    
    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return;
    }
    
    ChannelItem* item = (ChannelItem*)malloc(sizeof(ChannelItem));
    item->value = value;
    moon_retain(value);
    item->next = NULL;
    
    if (ch->tail) {
        ch->tail->next = item;
    } else {
        ch->head = item;
    }
    ch->tail = item;
    ch->count++;
    
    pthread_cond_signal(&ch->not_empty);
    
    if (ch->capacity == 0) {
        while (ch->count > 0 && !ch->closed) {
            pthread_cond_wait(&ch->not_full, &ch->lock);
        }
    }
    
    pthread_mutex_unlock(&ch->lock);
#endif
    
    // Auto-yield after send to give workers a chance to process
    moon_yield();
}

// Receive from channel: value = <- ch
MoonValue* moon_chan_recv(MoonValue* chVal) {
    MoonChannel* ch = get_channel(chVal);
    if (!ch) return moon_null();
    
    MoonValue* result = NULL;
    
#ifdef _WIN32
    EnterCriticalSection(&ch->lock);
    
    // Wait for item
    while (ch->count == 0 && !ch->closed) {
        SleepConditionVariableCS(&ch->not_empty, &ch->lock, INFINITE);
    }
    
    if (ch->count > 0) {
        ChannelItem* item = ch->head;
        result = item->value;
        ch->head = item->next;
        if (!ch->head) ch->tail = NULL;
        ch->count--;
        free(item);
        
        WakeConditionVariable(&ch->not_full);
    } else {
        result = moon_null();
    }
    
    LeaveCriticalSection(&ch->lock);
#else
    pthread_mutex_lock(&ch->lock);
    
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait(&ch->not_empty, &ch->lock);
    }
    
    if (ch->count > 0) {
        ChannelItem* item = ch->head;
        result = item->value;
        ch->head = item->next;
        if (!ch->head) ch->tail = NULL;
        ch->count--;
        free(item);
        
        pthread_cond_signal(&ch->not_full);
    } else {
        result = moon_null();
    }
    
    pthread_mutex_unlock(&ch->lock);
#endif
    
    return result ? result : moon_null();
}

// Close channel
void moon_chan_close(MoonValue* chVal) {
    MoonChannel* ch = get_channel(chVal);
    if (!ch) return;
    
#ifdef _WIN32
    EnterCriticalSection(&ch->lock);
    ch->closed = true;
    WakeAllConditionVariable(&ch->not_empty);
    WakeAllConditionVariable(&ch->not_full);
    LeaveCriticalSection(&ch->lock);
#else
    pthread_mutex_lock(&ch->lock);
    ch->closed = true;
    pthread_cond_broadcast(&ch->not_empty);
    pthread_cond_broadcast(&ch->not_full);
    pthread_mutex_unlock(&ch->lock);
#endif
}

// Check if channel is closed
MoonValue* moon_chan_is_closed(MoonValue* chVal) {
    MoonChannel* ch = get_channel(chVal);
    return moon_bool(ch ? ch->closed : true);
}
