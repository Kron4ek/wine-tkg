/* IDirectMusicPerformance Implementation
 *
 * Copyright (C) 2003-2004 Rok Mandeljc
 * Copyright (C) 2003-2004 Raphael Junqueira
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "dmime_private.h"
#include "wine/rbtree.h"
#include "dmobject.h"

WINE_DEFAULT_DEBUG_CHANNEL(dmime);

struct pchannel_block {
    DWORD block_num;   /* Block 0 is PChannels 0-15, Block 1 is PChannels 16-31, etc */
    struct {
       DWORD channel;  /* MIDI channel */
       DWORD group;    /* MIDI group */
       IDirectMusicPort *port;
    } pchannel[16];
    struct wine_rb_entry entry;
};

struct performance
{
    IDirectMusicPerformance8 IDirectMusicPerformance8_iface;
    IDirectMusicGraph IDirectMusicGraph_iface;
    IDirectMusicTool IDirectMusicTool_iface;
    LONG ref;
    IDirectMusic8 *dmusic;
    IDirectSound *dsound;
    IDirectMusicGraph *pToolGraph;
    DMUS_AUDIOPARAMS params;
    BOOL fAutoDownload;
    char cMasterGrooveLevel;
    float fMasterTempo;
    long lMasterVolume;
    /* performance channels */
    struct wine_rb_tree pchannels;

    IDirectMusicAudioPath *pDefaultPath;
    HANDLE hNotification;
    REFERENCE_TIME rtMinimum;
    REFERENCE_TIME rtLatencyTime;
    DWORD dwBumperLength;
    DWORD dwPrepareTime;
    /** Message Processing */
    HANDLE procThread;
    DWORD procThreadId;
    BOOL procThreadTicStarted;
    CRITICAL_SECTION safe;
    struct DMUS_PMSGItem *head;
    struct DMUS_PMSGItem *imm_head;

    IReferenceClock *master_clock;
    REFERENCE_TIME init_time;
};

typedef struct DMUS_PMSGItem DMUS_PMSGItem;
struct DMUS_PMSGItem {
  DMUS_PMSGItem* next;
  DMUS_PMSGItem* prev;

  REFERENCE_TIME rtItemTime;
  BOOL bInUse;
  DWORD cb;
  DMUS_PMSG pMsg;
};

#define DMUS_PMSGToItem(pMSG)   ((DMUS_PMSGItem *)(((unsigned char *)pMSG) - offsetof(DMUS_PMSGItem, pMsg)))
#define DMUS_ItemRemoveFromQueue(This,pItem) \
{\
  if (pItem->prev) pItem->prev->next = pItem->next;\
  if (pItem->next) pItem->next->prev = pItem->prev;\
  if (This->head == pItem) This->head = pItem->next;\
  if (This->imm_head == pItem) This->imm_head = pItem->next;\
  pItem->bInUse = FALSE;\
}

#define PROCESSMSG_START           (WM_APP + 0)
#define PROCESSMSG_EXIT            (WM_APP + 1)
#define PROCESSMSG_REMOVE          (WM_APP + 2)
#define PROCESSMSG_ADD             (WM_APP + 4)


static DMUS_PMSGItem* ProceedMsg(struct performance *This, DMUS_PMSGItem* cur) {
  if (cur->pMsg.dwType == DMUS_PMSGT_NOTIFICATION) {
    SetEvent(This->hNotification);
  }	
  DMUS_ItemRemoveFromQueue(This, cur);
  switch (cur->pMsg.dwType) {
  case DMUS_PMSGT_WAVE:
  case DMUS_PMSGT_TEMPO:   
  case DMUS_PMSGT_STOP:
  default:
    FIXME("Unhandled PMsg Type: %#lx\n", cur->pMsg.dwType);
    break;
  }
  return cur;
}

static DWORD WINAPI ProcessMsgThread(LPVOID lpParam) {
  struct performance *This = lpParam;
  DWORD timeOut = INFINITE;
  MSG msg;
  HRESULT hr;
  REFERENCE_TIME rtCurTime;
  DMUS_PMSGItem* it = NULL;
  DMUS_PMSGItem* cur = NULL;
  DMUS_PMSGItem* it_next = NULL;

  while (TRUE) {
    DWORD dwDec = This->rtLatencyTime + This->dwBumperLength;

    if (timeOut > 0) MsgWaitForMultipleObjects(0, NULL, FALSE, timeOut, QS_POSTMESSAGE|QS_SENDMESSAGE|QS_TIMER);
    timeOut = INFINITE;

    EnterCriticalSection(&This->safe);
    hr = IDirectMusicPerformance8_GetTime(&This->IDirectMusicPerformance8_iface, &rtCurTime, NULL);
    if (FAILED(hr)) {
      goto outrefresh;
    }
    
    for (it = This->imm_head; NULL != it; ) {
      it_next = it->next;
      cur = ProceedMsg(This, it);
      free(cur);
      it = it_next;
    }

    for (it = This->head; NULL != it && it->rtItemTime < rtCurTime + dwDec; ) {
      it_next = it->next;
      cur = ProceedMsg(This, it);
      free(cur);
      it = it_next;
    }
    if (NULL != it) {
      timeOut = ( it->rtItemTime - rtCurTime ) + This->rtLatencyTime;
    }

outrefresh:
    LeaveCriticalSection(&This->safe);
    
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
      /** if hwnd we suppose that is a windows event ... */
      if  (NULL != msg.hwnd) {
	TranslateMessage(&msg);
	DispatchMessageA(&msg);
      } else {
	switch (msg.message) {	    
	case WM_QUIT:
	case PROCESSMSG_EXIT:
	  goto outofthread;
	case PROCESSMSG_START:
	  break;
	case PROCESSMSG_ADD:
	  break;
	case PROCESSMSG_REMOVE:
	  break;
	default:
	  ERR("Unhandled message %u. Critical Path\n", msg.message);
	  break;
	}
      }
    }

    /** here we should run a little of current AudioPath */

  }

outofthread:
  TRACE("(%p): Exiting\n", This);
  
  return 0;
}

static BOOL PostMessageToProcessMsgThread(struct performance *This, UINT iMsg) {
  if (FALSE == This->procThreadTicStarted && PROCESSMSG_EXIT != iMsg) {
    BOOL res;
    This->procThread = CreateThread(NULL, 0, ProcessMsgThread, This, 0, &This->procThreadId);
    if (NULL == This->procThread) return FALSE;
    SetThreadPriority(This->procThread, THREAD_PRIORITY_TIME_CRITICAL);
    This->procThreadTicStarted = TRUE;
    while(1) {
      res = PostThreadMessageA(This->procThreadId, iMsg, 0, 0);
      /* Let the thread creates its message queue (with MsgWaitForMultipleObjects call) by yielding and retrying */
      if (!res && (GetLastError() == ERROR_INVALID_THREAD_ID))
	Sleep(0);
      else
	break;
    }
    return res;
  }
  return PostThreadMessageA(This->procThreadId, iMsg, 0, 0);
}

static int pchannel_block_compare(const void *key, const struct wine_rb_entry *entry)
{
    const struct pchannel_block *b = WINE_RB_ENTRY_VALUE(entry, const struct pchannel_block, entry);

    return *(DWORD *)key - b->block_num;
}

static void pchannel_block_free(struct wine_rb_entry *entry, void *context)
{
    struct pchannel_block *b = WINE_RB_ENTRY_VALUE(entry, struct pchannel_block, entry);

    free(b);
}

static struct pchannel_block *pchannel_block_set(struct wine_rb_tree *tree, DWORD block_num,
        IDirectMusicPort *port, DWORD group, BOOL only_set_new)
{
    struct pchannel_block *block;
    struct wine_rb_entry *entry;
    unsigned int i;

    entry = wine_rb_get(tree, &block_num);
    if (entry) {
        block = WINE_RB_ENTRY_VALUE(entry, struct pchannel_block, entry);
        if (only_set_new)
            return block;
    } else {
        if (!(block = malloc(sizeof(*block)))) return NULL;
        block->block_num = block_num;
    }

    for (i = 0; i < 16; ++i) {
        block->pchannel[i].port = port;
        block->pchannel[i].group = group;
        block->pchannel[i].channel = i;
    }
    if (!entry)
        wine_rb_put(tree, &block->block_num, &block->entry);

    return block;
}

static inline struct performance *impl_from_IDirectMusicPerformance8(IDirectMusicPerformance8 *iface)
{
    return CONTAINING_RECORD(iface, struct performance, IDirectMusicPerformance8_iface);
}

IDirectSound *get_dsound_interface(IDirectMusicPerformance8* iface)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);
    return This->dsound;
}


/* IDirectMusicPerformance8 IUnknown part: */
static HRESULT WINAPI performance_QueryInterface(IDirectMusicPerformance8 *iface, REFIID riid, void **ret_iface)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);

    TRACE("(%p, %s, %p)\n", iface, debugstr_dmguid(riid), ret_iface);

    if (IsEqualGUID(riid, &IID_IUnknown)
            || IsEqualGUID(riid, &IID_IDirectMusicPerformance)
            || IsEqualGUID(riid, &IID_IDirectMusicPerformance2)
            || IsEqualGUID(riid, &IID_IDirectMusicPerformance8))
    {
        *ret_iface = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_IDirectMusicGraph))
    {
        *ret_iface = &This->IDirectMusicGraph_iface;
        IDirectMusicGraph_AddRef(&This->IDirectMusicGraph_iface);
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_IDirectMusicTool))
    {
        *ret_iface = &This->IDirectMusicTool_iface;
        IDirectMusicTool_AddRef(&This->IDirectMusicTool_iface);
        return S_OK;
    }

    *ret_iface = NULL;
    WARN("(%p, %s, %p): not found\n", iface, debugstr_dmguid(riid), ret_iface);
    return E_NOINTERFACE;
}

static ULONG WINAPI performance_AddRef(IDirectMusicPerformance8 *iface)
{
  struct performance *This = impl_from_IDirectMusicPerformance8(iface);
  ULONG ref = InterlockedIncrement(&This->ref);

  TRACE("(%p): ref=%ld\n", This, ref);

  return ref;
}

static ULONG WINAPI performance_Release(IDirectMusicPerformance8 *iface)
{
  struct performance *This = impl_from_IDirectMusicPerformance8(iface);
  ULONG ref = InterlockedDecrement(&This->ref);

  TRACE("(%p): ref=%ld\n", This, ref);
  
  if (ref == 0) {
    wine_rb_destroy(&This->pchannels, pchannel_block_free, NULL);
    This->safe.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection(&This->safe);
    free(This);
  }

  return ref;
}

/* IDirectMusicPerformanceImpl IDirectMusicPerformance Interface part: */
static HRESULT WINAPI performance_Init(IDirectMusicPerformance8 *iface, IDirectMusic **dmusic,
        IDirectSound *dsound, HWND hwnd)
{
    TRACE("(%p, %p, %p, %p)\n", iface, dmusic, dsound, hwnd);

    return IDirectMusicPerformance8_InitAudio(iface, dmusic, dsound ? &dsound : NULL, hwnd, 0, 0,
            0, NULL);
}

static HRESULT WINAPI performance_PlaySegment(IDirectMusicPerformance8 *iface, IDirectMusicSegment *pSegment,
        DWORD dwFlags, __int64 i64StartTime, IDirectMusicSegmentState **ppSegmentState)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %p, %ld, 0x%s, %p): stub\n", This, pSegment, dwFlags,
	    wine_dbgstr_longlong(i64StartTime), ppSegmentState);
	if (ppSegmentState)
          return create_dmsegmentstate(&IID_IDirectMusicSegmentState,(void**)ppSegmentState);
	return S_OK;
}

static HRESULT WINAPI performance_Stop(IDirectMusicPerformance8 *iface, IDirectMusicSegment *pSegment,
        IDirectMusicSegmentState *pSegmentState, MUSIC_TIME mtTime, DWORD dwFlags)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %p, %p, %ld, %ld): stub\n", This, pSegment, pSegmentState, mtTime, dwFlags);
	return S_OK;
}

static HRESULT WINAPI performance_GetSegmentState(IDirectMusicPerformance8 *iface,
        IDirectMusicSegmentState **ppSegmentState, MUSIC_TIME mtTime)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p,%p, %ld): stub\n", This, ppSegmentState, mtTime);
	return S_OK;
}

static HRESULT WINAPI performance_SetPrepareTime(IDirectMusicPerformance8 *iface, DWORD dwMilliSeconds)
{
  struct performance *This = impl_from_IDirectMusicPerformance8(iface);

  TRACE("(%p, %ld)\n", This, dwMilliSeconds);
  This->dwPrepareTime = dwMilliSeconds;
  return S_OK;
}

static HRESULT WINAPI performance_GetPrepareTime(IDirectMusicPerformance8 *iface, DWORD *pdwMilliSeconds)
{
  struct performance *This = impl_from_IDirectMusicPerformance8(iface);

  TRACE("(%p, %p)\n", This, pdwMilliSeconds);
  if (NULL == pdwMilliSeconds) {
    return E_POINTER;
  }
  *pdwMilliSeconds = This->dwPrepareTime;
  return S_OK;
}

static HRESULT WINAPI performance_SetBumperLength(IDirectMusicPerformance8 *iface, DWORD dwMilliSeconds)
{
  struct performance *This = impl_from_IDirectMusicPerformance8(iface);

  TRACE("(%p, %ld)\n", This, dwMilliSeconds);
  This->dwBumperLength =  dwMilliSeconds;
  return S_OK;
}

static HRESULT WINAPI performance_GetBumperLength(IDirectMusicPerformance8 *iface, DWORD *pdwMilliSeconds)
{
  struct performance *This = impl_from_IDirectMusicPerformance8(iface);

  TRACE("(%p, %p)\n", This, pdwMilliSeconds);
  if (NULL == pdwMilliSeconds) {
    return E_POINTER;
  }
  *pdwMilliSeconds = This->dwBumperLength;
  return S_OK;
}

static HRESULT WINAPI performance_SendPMsg(IDirectMusicPerformance8 *iface, DMUS_PMSG *msg)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);
    DMUS_PMSGItem *message;
    DMUS_PMSGItem *it = NULL;
    DMUS_PMSGItem *prev_it = NULL;
    DMUS_PMSGItem **queue;
    HRESULT hr;

    FIXME("(%p, %p): semi-stub\n", This, msg);

    if (!msg) return E_POINTER;
    if (!This->dmusic) return DMUS_E_NO_MASTER_CLOCK;
    if (!(msg->dwFlags & (DMUS_PMSGF_MUSICTIME | DMUS_PMSGF_REFTIME))) return E_INVALIDARG;

    if (msg->dwFlags & DMUS_PMSGF_TOOL_IMMEDIATE) queue = &This->imm_head;
    else queue = &This->head;

    message = DMUS_PMSGToItem(msg);

    EnterCriticalSection(&This->safe);

    if (message->bInUse)
        hr = DMUS_E_ALREADY_SENT;
    else
    {
        /* TODO: Valid Flags */
        /* TODO: DMUS_PMSGF_MUSICTIME */
        message->rtItemTime = msg->rtTime;

        for (it = *queue; NULL != it && it->rtItemTime < message->rtItemTime; it = it->next)
            prev_it = it;

        if (!prev_it)
        {
            message->prev = NULL;
            if (*queue) message->next = (*queue)->next;
            /*assert( NULL == message->next->prev );*/
            if (message->next) message->next->prev = message;
            *queue = message;
        }
        else
        {
            message->prev = prev_it;
            message->next = prev_it->next;
            prev_it->next = message;
            if (message->next) message->next->prev = message;
        }

        message->bInUse = TRUE;
        hr = S_OK;
    }

    LeaveCriticalSection(&This->safe);

    return hr;
}

static HRESULT WINAPI performance_MusicToReferenceTime(IDirectMusicPerformance8 *iface,
        MUSIC_TIME music_time, REFERENCE_TIME *time)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);

    FIXME("(%p, %ld, %p): semi-stub\n", This, music_time, time);

    if (!time) return E_POINTER;
    *time = 0;

    if (!This->master_clock) return DMUS_E_NO_MASTER_CLOCK;

    /* FIXME: This should be (music_time * 60) / (DMUS_PPQ * tempo)
     * but it gives innacurate results */
    *time = This->init_time + (music_time * 6510);

    return S_OK;
}

static HRESULT WINAPI performance_ReferenceToMusicTime(IDirectMusicPerformance8 *iface,
        REFERENCE_TIME time, MUSIC_TIME *music_time)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);

    FIXME("(%p, %I64d, %p): semi-stub\n", This, time, music_time);

    if (!music_time) return E_POINTER;
    *music_time = 0;

    if (!This->master_clock) return DMUS_E_NO_MASTER_CLOCK;

    /* FIXME: This should be (time * DMUS_PPQ * tempo) / 60
     * but it gives innacurate results */
    *music_time = (time - This->init_time) / 6510;

    return S_OK;
}

static HRESULT WINAPI performance_IsPlaying(IDirectMusicPerformance8 *iface,
        IDirectMusicSegment *pSegment, IDirectMusicSegmentState *pSegState)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %p, %p): stub\n", This, pSegment, pSegState);
	return S_FALSE;
}

static HRESULT WINAPI performance_GetTime(IDirectMusicPerformance8 *iface, REFERENCE_TIME *time, MUSIC_TIME *music_time)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);
    REFERENCE_TIME now;
    HRESULT hr;

    TRACE("(%p, %p, %p)\n", iface, time, music_time);

    if (!This->master_clock) return DMUS_E_NO_MASTER_CLOCK;
    if (FAILED(hr = IReferenceClock_GetTime(This->master_clock, &now))) return hr;

    if (time) *time = now;
    if (music_time) hr = IDirectMusicPerformance8_ReferenceToMusicTime(iface, now, music_time);

    return hr;
}

static HRESULT WINAPI performance_AllocPMsg(IDirectMusicPerformance8 *iface, ULONG size, DMUS_PMSG **msg)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);
    DMUS_PMSGItem *message;

    TRACE("(%p, %ld, %p)\n", This, size, msg);

    if (!msg) return E_POINTER;
    if (size < sizeof(DMUS_PMSG)) return E_INVALIDARG;

    if (!(message = calloc(1, size - sizeof(DMUS_PMSG) + sizeof(DMUS_PMSGItem)))) return E_OUTOFMEMORY;
    message->pMsg.dwSize = size;
    *msg = &message->pMsg;

    return S_OK;
}

static HRESULT WINAPI performance_FreePMsg(IDirectMusicPerformance8 *iface, DMUS_PMSG *msg)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);
    DMUS_PMSGItem *message;
    HRESULT hr;

    TRACE("(%p, %p)\n", This, msg);

    if (!msg) return E_POINTER;
    message = DMUS_PMSGToItem(msg);

    EnterCriticalSection(&This->safe);
    hr = message->bInUse ? DMUS_E_CANNOT_FREE : S_OK;
    LeaveCriticalSection(&This->safe);

    if (SUCCEEDED(hr))
    {
        if (msg->pTool) IDirectMusicTool_Release(msg->pTool);
        if (msg->pGraph) IDirectMusicGraph_Release(msg->pGraph);
        if (msg->punkUser) IUnknown_Release(msg->punkUser);
        free(message);
    }

    return hr;
}

static HRESULT WINAPI performance_GetGraph(IDirectMusicPerformance8 *iface, IDirectMusicGraph **graph)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);

    TRACE("(%p, %p)\n", This, graph);

    if (!graph)
        return E_POINTER;

    *graph = This->pToolGraph;
    if (This->pToolGraph) {
        IDirectMusicGraph_AddRef(*graph);
    }

    return *graph ? S_OK : DMUS_E_NOT_FOUND;
}

static HRESULT WINAPI performance_SetGraph(IDirectMusicPerformance8 *iface, IDirectMusicGraph *pGraph)
{
  struct performance *This = impl_from_IDirectMusicPerformance8(iface);

  FIXME("(%p, %p): to check\n", This, pGraph);
  
  if (NULL != This->pToolGraph) {
    /* Todo clean buffers and tools before */
    IDirectMusicGraph_Release(This->pToolGraph);
  }
  This->pToolGraph = pGraph;
  if (NULL != This->pToolGraph) {
    IDirectMusicGraph_AddRef(This->pToolGraph);
  }
  return S_OK;
}

static HRESULT WINAPI performance_SetNotificationHandle(IDirectMusicPerformance8 *iface,
        HANDLE hNotification, REFERENCE_TIME rtMinimum)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);

    TRACE("(%p, %p, 0x%s)\n", This, hNotification, wine_dbgstr_longlong(rtMinimum));

    This->hNotification = hNotification;
    if (rtMinimum)
        This->rtMinimum = rtMinimum;
    else if (!This->rtMinimum)
        This->rtMinimum = 20000000; /* 2 seconds */
    return S_OK;
}

static HRESULT WINAPI performance_GetNotificationPMsg(IDirectMusicPerformance8 *iface,
        DMUS_NOTIFICATION_PMSG **ppNotificationPMsg)
{
  struct performance *This = impl_from_IDirectMusicPerformance8(iface);

  FIXME("(%p, %p): stub\n", This, ppNotificationPMsg);
  if (NULL == ppNotificationPMsg) {
    return E_POINTER;
  }
  
  

  return S_FALSE;
  /*return S_OK;*/
}

static HRESULT WINAPI performance_AddNotificationType(IDirectMusicPerformance8 *iface, REFGUID rguidNotificationType)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %s): stub\n", This, debugstr_dmguid(rguidNotificationType));
	return S_OK;
}

static HRESULT WINAPI performance_RemoveNotificationType(IDirectMusicPerformance8 *iface, REFGUID rguidNotificationType)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %s): stub\n", This, debugstr_dmguid(rguidNotificationType));
	return S_OK;
}

static HRESULT perf_dmport_create(struct performance *perf, DMUS_PORTPARAMS *params)
{
    IDirectMusicPort *port;
    GUID guid;
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = IDirectMusic8_GetDefaultPort(perf->dmusic, &guid)))
        return hr;

    if (FAILED(hr = IDirectMusic8_CreatePort(perf->dmusic, &guid, params, &port, NULL)))
        return hr;
    if (FAILED(hr = IDirectMusicPort_Activate(port, TRUE))) {
        IDirectMusicPort_Release(port);
        return hr;
    }
    for (i = 0; i < params->dwChannelGroups; i++)
        pchannel_block_set(&perf->pchannels, i, port, i + 1, FALSE);

    return S_OK;
}

static HRESULT WINAPI performance_AddPort(IDirectMusicPerformance8 *iface, IDirectMusicPort *port)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);

    FIXME("(%p, %p): semi-stub\n", This, port);

    if (!This->dmusic)
        return DMUS_E_NOT_INIT;

    if (!port) {
        DMUS_PORTPARAMS params = {
            .dwSize = sizeof(params),
            .dwValidParams = DMUS_PORTPARAMS_CHANNELGROUPS,
            .dwChannelGroups = 1
        };

        return perf_dmport_create(This, &params);
    }

    IDirectMusicPort_AddRef(port);
    /**
     * We should remember added Ports (for example using a list)
     * and control if Port is registered for each api who use ports
     */
    return S_OK;
}

static HRESULT WINAPI performance_RemovePort(IDirectMusicPerformance8 *iface, IDirectMusicPort *pPort)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %p): stub\n", This, pPort);
	IDirectMusicPort_Release (pPort);
	return S_OK;
}

static HRESULT WINAPI performance_AssignPChannelBlock(IDirectMusicPerformance8 *iface,
        DWORD block_num, IDirectMusicPort *port, DWORD group)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);

    FIXME("(%p, %ld, %p, %ld): semi-stub\n", This, block_num, port, group);

    if (!port)
        return E_POINTER;
    if (block_num > MAXDWORD / 16)
        return E_INVALIDARG;

    pchannel_block_set(&This->pchannels, block_num, port, group, FALSE);

    return S_OK;
}

static HRESULT WINAPI performance_AssignPChannel(IDirectMusicPerformance8 *iface, DWORD pchannel,
        IDirectMusicPort *port, DWORD group, DWORD channel)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);
    struct pchannel_block *block;

    FIXME("(%p)->(%ld, %p, %ld, %ld) semi-stub\n", This, pchannel, port, group, channel);

    if (!port)
        return E_POINTER;

    block = pchannel_block_set(&This->pchannels, pchannel / 16, port, 0, TRUE);
    if (block) {
        block->pchannel[pchannel % 16].group = group;
        block->pchannel[pchannel % 16].channel = channel;
    }

    return S_OK;
}

static HRESULT WINAPI performance_PChannelInfo(IDirectMusicPerformance8 *iface, DWORD pchannel,
        IDirectMusicPort **port, DWORD *group, DWORD *channel)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);
    struct pchannel_block *block;
    struct wine_rb_entry *entry;
    DWORD block_num = pchannel / 16;
    unsigned int index = pchannel % 16;

    TRACE("(%p)->(%ld, %p, %p, %p)\n", This, pchannel, port, group, channel);

    entry = wine_rb_get(&This->pchannels, &block_num);
    if (!entry)
        return E_INVALIDARG;
    block = WINE_RB_ENTRY_VALUE(entry, struct pchannel_block, entry);

    if (port) {
        *port = block->pchannel[index].port;
        IDirectMusicPort_AddRef(*port);
    }
    if (group)
        *group = block->pchannel[index].group;
    if (channel)
        *channel = block->pchannel[index].channel;

    return S_OK;
}

static HRESULT WINAPI performance_DownloadInstrument(IDirectMusicPerformance8 *iface,
        IDirectMusicInstrument *pInst, DWORD dwPChannel,
        IDirectMusicDownloadedInstrument **ppDownInst, DMUS_NOTERANGE *pNoteRanges,
        DWORD dwNumNoteRanges, IDirectMusicPort **ppPort, DWORD *pdwGroup, DWORD *pdwMChannel)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %p, %ld, %p, %p, %ld, %p, %p, %p): stub\n", This, pInst, dwPChannel, ppDownInst, pNoteRanges, dwNumNoteRanges, ppPort, pdwGroup, pdwMChannel);
	return S_OK;
}

static HRESULT WINAPI performance_Invalidate(IDirectMusicPerformance8 *iface, MUSIC_TIME mtTime, DWORD dwFlags)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %ld, %ld): stub\n", This, mtTime, dwFlags);
	return S_OK;
}

static HRESULT WINAPI performance_GetParam(IDirectMusicPerformance8 *iface, REFGUID rguidType,
        DWORD dwGroupBits, DWORD dwIndex, MUSIC_TIME mtTime, MUSIC_TIME *pmtNext, void *pParam)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %s, %ld, %ld, %ld, %p, %p): stub\n", This, debugstr_dmguid(rguidType), dwGroupBits, dwIndex, mtTime, pmtNext, pParam);
	return S_OK;
}

static HRESULT WINAPI performance_SetParam(IDirectMusicPerformance8 *iface, REFGUID rguidType,
        DWORD dwGroupBits, DWORD dwIndex, MUSIC_TIME mtTime, void *pParam)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %s, %ld, %ld, %ld, %p): stub\n", This, debugstr_dmguid(rguidType), dwGroupBits, dwIndex, mtTime, pParam);
	return S_OK;
}

static HRESULT WINAPI performance_GetGlobalParam(IDirectMusicPerformance8 *iface, REFGUID rguidType,
        void *pParam, DWORD dwSize)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	TRACE("(%p, %s, %p, %ld): stub\n", This, debugstr_dmguid(rguidType), pParam, dwSize);

	if (IsEqualGUID (rguidType, &GUID_PerfAutoDownload))
		memcpy(pParam, &This->fAutoDownload, sizeof(This->fAutoDownload));
	if (IsEqualGUID (rguidType, &GUID_PerfMasterGrooveLevel))
		memcpy(pParam, &This->cMasterGrooveLevel, sizeof(This->cMasterGrooveLevel));
	if (IsEqualGUID (rguidType, &GUID_PerfMasterTempo))
		memcpy(pParam, &This->fMasterTempo, sizeof(This->fMasterTempo));
	if (IsEqualGUID (rguidType, &GUID_PerfMasterVolume))
		memcpy(pParam, &This->lMasterVolume, sizeof(This->lMasterVolume));

	return S_OK;
}

static HRESULT WINAPI performance_SetGlobalParam(IDirectMusicPerformance8 *iface, REFGUID rguidType,
        void *pParam, DWORD dwSize)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	TRACE("(%p, %s, %p, %ld)\n", This, debugstr_dmguid(rguidType), pParam, dwSize);

	if (IsEqualGUID (rguidType, &GUID_PerfAutoDownload)) {
		memcpy(&This->fAutoDownload, pParam, dwSize);
		TRACE("=> AutoDownload set to %d\n", This->fAutoDownload);
	}
	if (IsEqualGUID (rguidType, &GUID_PerfMasterGrooveLevel)) {
		memcpy(&This->cMasterGrooveLevel, pParam, dwSize);
		TRACE("=> MasterGrooveLevel set to %i\n", This->cMasterGrooveLevel);
	}
	if (IsEqualGUID (rguidType, &GUID_PerfMasterTempo)) {
		memcpy(&This->fMasterTempo, pParam, dwSize);
		TRACE("=> MasterTempo set to %f\n", This->fMasterTempo);
	}
	if (IsEqualGUID (rguidType, &GUID_PerfMasterVolume)) {
		memcpy(&This->lMasterVolume, pParam, dwSize);
		TRACE("=> MasterVolume set to %li\n", This->lMasterVolume);
	}

	return S_OK;
}

static HRESULT WINAPI performance_GetLatencyTime(IDirectMusicPerformance8 *iface, REFERENCE_TIME *prtTime)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	TRACE("(%p, %p): stub\n", This, prtTime);
	*prtTime = This->rtLatencyTime;
	return S_OK;
}

static HRESULT WINAPI performance_GetQueueTime(IDirectMusicPerformance8 *iface, REFERENCE_TIME *prtTime)

{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %p): stub\n", This, prtTime);
	return S_OK;
}

static HRESULT WINAPI performance_AdjustTime(IDirectMusicPerformance8 *iface, REFERENCE_TIME rtAmount)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, 0x%s): stub\n", This, wine_dbgstr_longlong(rtAmount));
	return S_OK;
}

static HRESULT WINAPI performance_CloseDown(IDirectMusicPerformance8 *iface)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);

    FIXME("(%p): semi-stub\n", This);

    if (PostMessageToProcessMsgThread(This, PROCESSMSG_EXIT)) {
        WaitForSingleObject(This->procThread, INFINITE);
        This->procThreadTicStarted = FALSE;
        CloseHandle(This->procThread);
    }
    if (This->master_clock)
    {
        IReferenceClock_Release(This->master_clock);
        This->master_clock = NULL;
    }
    if (This->dsound) {
        IDirectSound_Release(This->dsound);
        This->dsound = NULL;
    }
    if (This->dmusic) {
        IDirectMusic8_SetDirectSound(This->dmusic, NULL, NULL);
        IDirectMusic8_Release(This->dmusic);
        This->dmusic = NULL;
    }
    return S_OK;
}

static HRESULT WINAPI performance_GetResolvedTime(IDirectMusicPerformance8 *iface,
        REFERENCE_TIME rtTime, REFERENCE_TIME *prtResolved, DWORD dwTimeResolveFlags)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, 0x%s, %p, %ld): stub\n", This, wine_dbgstr_longlong(rtTime),
	    prtResolved, dwTimeResolveFlags);
	return S_OK;
}

static HRESULT WINAPI performance_MIDIToMusic(IDirectMusicPerformance8 *iface, BYTE bMIDIValue,
        DMUS_CHORD_KEY *pChord, BYTE bPlayMode, BYTE bChordLevel, WORD *pwMusicValue)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %d, %p, %d, %d, %p): stub\n", This, bMIDIValue, pChord, bPlayMode, bChordLevel, pwMusicValue);
	return S_OK;
}

static HRESULT WINAPI performance_MusicToMIDI(IDirectMusicPerformance8 *iface, WORD wMusicValue,
        DMUS_CHORD_KEY *pChord, BYTE bPlayMode, BYTE bChordLevel, BYTE *pbMIDIValue)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %d, %p, %d, %d, %p): stub\n", This, wMusicValue, pChord, bPlayMode, bChordLevel, pbMIDIValue);
	return S_OK;
}

static HRESULT WINAPI performance_TimeToRhythm(IDirectMusicPerformance8 *iface, MUSIC_TIME mtTime,
        DMUS_TIMESIGNATURE *pTimeSig, WORD *pwMeasure, BYTE *pbBeat, BYTE *pbGrid, short *pnOffset)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %ld, %p, %p, %p, %p, %p): stub\n", This, mtTime, pTimeSig, pwMeasure, pbBeat, pbGrid, pnOffset);
	return S_OK;
}

static HRESULT WINAPI performance_RhythmToTime(IDirectMusicPerformance8 *iface, WORD wMeasure,
        BYTE bBeat, BYTE bGrid, short nOffset, DMUS_TIMESIGNATURE *pTimeSig, MUSIC_TIME *pmtTime)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %d, %d, %d, %i, %p, %p): stub\n", This, wMeasure, bBeat, bGrid, nOffset, pTimeSig, pmtTime);
	return S_OK;
}

/* IDirectMusicPerformance8 Interface part follow: */
static HRESULT WINAPI performance_InitAudio(IDirectMusicPerformance8 *iface, IDirectMusic **dmusic,
        IDirectSound **dsound, HWND hwnd, DWORD default_path_type, DWORD num_channels, DWORD flags,
        DMUS_AUDIOPARAMS *params)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);
    HRESULT hr = S_OK;

    TRACE("(%p, %p, %p, %p, %lx, %lu, %lx, %p)\n", This, dmusic, dsound, hwnd, default_path_type,
            num_channels, flags, params);

    if (This->dmusic)
        return DMUS_E_ALREADY_INITED;

    if (!dmusic || !*dmusic) {
        hr = CoCreateInstance(&CLSID_DirectMusic, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectMusic8,
                (void **)&This->dmusic);
        if (FAILED(hr))
            return hr;
    } else {
        This->dmusic = (IDirectMusic8 *)*dmusic;
        IDirectMusic8_AddRef(This->dmusic);
    }

    if (FAILED(hr = IDirectMusic_GetMasterClock(This->dmusic, NULL, &This->master_clock)))
        goto error;

    if (!dsound || !*dsound) {
        hr = DirectSoundCreate8(NULL, (IDirectSound8 **)&This->dsound, NULL);
        if (FAILED(hr))
            goto error;
        hr = IDirectSound_SetCooperativeLevel(This->dsound, hwnd ? hwnd : GetForegroundWindow(),
                DSSCL_PRIORITY);
        if (FAILED(hr))
            goto error;
    } else {
        This->dsound = *dsound;
        IDirectSound_AddRef(This->dsound);
    }

    hr = IDirectMusic8_SetDirectSound(This->dmusic, This->dsound, NULL);
    if (FAILED(hr))
        goto error;

    if (!params) {
        This->params.dwSize = sizeof(DMUS_AUDIOPARAMS);
        This->params.fInitNow = FALSE;
        This->params.dwValidData = DMUS_AUDIOPARAMS_FEATURES | DMUS_AUDIOPARAMS_VOICES |
                DMUS_AUDIOPARAMS_SAMPLERATE | DMUS_AUDIOPARAMS_DEFAULTSYNTH;
        This->params.dwVoices = 64;
        This->params.dwSampleRate = 22050;
        This->params.dwFeatures = flags;
        This->params.clsidDefaultSynth = CLSID_DirectMusicSynthSink;
    } else
        This->params = *params;

    if (default_path_type) {
        hr = IDirectMusicPerformance8_CreateStandardAudioPath(iface, default_path_type,
                num_channels, FALSE, &This->pDefaultPath);
        if (FAILED(hr)) {
            IDirectMusic8_SetDirectSound(This->dmusic, NULL, NULL);
            goto error;
        }
    }

    if (dsound && !*dsound) {
        *dsound = This->dsound;
        IDirectSound_AddRef(*dsound);
    }
    if (dmusic && !*dmusic) {
        *dmusic = (IDirectMusic *)This->dmusic;
        IDirectMusic_AddRef(*dmusic);
    }

    if (FAILED(hr = IDirectMusicPerformance8_GetTime(iface, &This->init_time, NULL))) return hr;

    PostMessageToProcessMsgThread(This, PROCESSMSG_START);

    return S_OK;

error:
    if (This->master_clock)
    {
        IReferenceClock_Release(This->master_clock);
        This->master_clock = NULL;
    }
    if (This->dsound) {
        IDirectSound_Release(This->dsound);
        This->dsound = NULL;
    }
    if (This->dmusic) {
        IDirectMusic8_Release(This->dmusic);
        This->dmusic = NULL;
    }
    return hr;
}

static HRESULT WINAPI performance_PlaySegmentEx(IDirectMusicPerformance8 *iface, IUnknown *pSource,
        WCHAR *pwzSegmentName, IUnknown *pTransition, DWORD dwFlags, __int64 i64StartTime,
        IDirectMusicSegmentState **ppSegmentState, IUnknown *pFrom, IUnknown *pAudioPath)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);
    IDirectMusicSegment8 *segment;
    IDirectSoundBuffer *buffer;
    HRESULT hr;

    FIXME("(%p, %p, %p, %p, %ld, 0x%s, %p, %p, %p): semi-stub\n", This, pSource, pwzSegmentName,
        pTransition, dwFlags, wine_dbgstr_longlong(i64StartTime), ppSegmentState, pFrom, pAudioPath);

    hr = IUnknown_QueryInterface(pSource, &IID_IDirectMusicSegment8, (void**)&segment);
    if (FAILED(hr))
        return hr;

    buffer = get_segment_buffer(segment);

    if (segment)
        hr = IDirectSoundBuffer_Play(buffer, 0, 0, 0);

    if (ppSegmentState)
      return create_dmsegmentstate(&IID_IDirectMusicSegmentState,(void**)ppSegmentState);
    return S_OK;
}

static HRESULT WINAPI performance_StopEx(IDirectMusicPerformance8 *iface, IUnknown *pObjectToStop,
        __int64 i64StopTime, DWORD dwFlags)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %p, 0x%s, %ld): stub\n", This, pObjectToStop,
	    wine_dbgstr_longlong(i64StopTime), dwFlags);
	return S_OK;
}

static HRESULT WINAPI performance_ClonePMsg(IDirectMusicPerformance8 *iface, DMUS_PMSG *msg, DMUS_PMSG **clone)
{
    struct performance *This = impl_from_IDirectMusicPerformance8(iface);
    HRESULT hr;

    TRACE("(%p, %p, %p)\n", This, msg, clone);

    if (!msg || !clone) return E_POINTER;
    if (FAILED(hr = IDirectMusicPerformance8_AllocPMsg(iface, msg->dwSize, clone))) return hr;

    memcpy(*clone, msg, msg->dwSize);
    if (msg->pTool) IDirectMusicTool_AddRef(msg->pTool);
    if (msg->pGraph) IDirectMusicGraph_AddRef(msg->pGraph);
    if (msg->punkUser) IUnknown_AddRef(msg->punkUser);

    return S_OK;
}

static HRESULT WINAPI performance_CreateAudioPath(IDirectMusicPerformance8 *iface,
        IUnknown *pSourceConfig, BOOL fActivate, IDirectMusicAudioPath **ppNewPath)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);
	IDirectMusicAudioPath *pPath;

	FIXME("(%p, %p, %d, %p): stub\n", This, pSourceConfig, fActivate, ppNewPath);

	if (NULL == ppNewPath) {
	  return E_POINTER;
	}

        create_dmaudiopath(&IID_IDirectMusicAudioPath, (void**)&pPath);
        set_audiopath_perf_pointer(pPath, iface);

	/** TODO */
	
	*ppNewPath = pPath;

	return IDirectMusicAudioPath_Activate(*ppNewPath, fActivate);
}

static HRESULT WINAPI performance_CreateStandardAudioPath(IDirectMusicPerformance8 *iface,
        DWORD dwType, DWORD pchannel_count, BOOL fActivate, IDirectMusicAudioPath **ppNewPath)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);
	IDirectMusicAudioPath *pPath;
	DSBUFFERDESC desc;
	WAVEFORMATEX format;
        DMUS_PORTPARAMS params = {0};
	IDirectSoundBuffer *buffer, *primary_buffer;
	HRESULT hr = S_OK;

        FIXME("(%p)->(%ld, %ld, %d, %p): semi-stub\n", This, dwType, pchannel_count, fActivate, ppNewPath);

	if (NULL == ppNewPath) {
	  return E_POINTER;
	}

        *ppNewPath = NULL;

	/* Secondary buffer description */
	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 1;
	format.nSamplesPerSec = 44000;
	format.nAvgBytesPerSec = 44000*2;
	format.nBlockAlign = 2;
	format.wBitsPerSample = 16;
	format.cbSize = 0;
	
	memset(&desc, 0, sizeof(desc));
	desc.dwSize = sizeof(desc);
        desc.dwFlags = DSBCAPS_CTRLFX | DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS;
	desc.dwBufferBytes = DSBSIZE_MIN;
	desc.dwReserved = 0;
	desc.lpwfxFormat = &format;
	desc.guid3DAlgorithm = GUID_NULL;
	
	switch(dwType) {
	case DMUS_APATH_DYNAMIC_3D:
                desc.dwFlags |= DSBCAPS_CTRL3D | DSBCAPS_CTRLFREQUENCY | DSBCAPS_MUTE3DATMAXDISTANCE;
		break;
	case DMUS_APATH_DYNAMIC_MONO:
                desc.dwFlags |= DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;
		break;
	case DMUS_APATH_SHARED_STEREOPLUSREVERB:
	        /* normally we have to create 2 buffers (one for music other for reverb)
		 * in this case. See msdn
                 */
	case DMUS_APATH_DYNAMIC_STEREO:
                desc.dwFlags |= DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;
		format.nChannels = 2;
		format.nBlockAlign *= 2;
		format.nAvgBytesPerSec *=2;
		break;
	default:
	        return E_INVALIDARG;
	}

        /* Create a port */
        params.dwSize = sizeof(params);
        params.dwValidParams = DMUS_PORTPARAMS_CHANNELGROUPS | DMUS_PORTPARAMS_AUDIOCHANNELS;
        params.dwChannelGroups = (pchannel_count + 15) / 16;
        params.dwAudioChannels = format.nChannels;
        if (FAILED(hr = perf_dmport_create(This, &params)))
                return hr;

        hr = IDirectSound_CreateSoundBuffer(This->dsound, &desc, &buffer, NULL);
	if (FAILED(hr))
	        return DSERR_BUFFERLOST;

	/* Update description for creating primary buffer */
	desc.dwFlags |= DSBCAPS_PRIMARYBUFFER;
	desc.dwFlags &= ~DSBCAPS_CTRLFX;
	desc.dwBufferBytes = 0;
	desc.lpwfxFormat = NULL;

        hr = IDirectSound_CreateSoundBuffer(This->dsound, &desc, &primary_buffer, NULL);
	if (FAILED(hr)) {
                IDirectSoundBuffer_Release(buffer);
	        return DSERR_BUFFERLOST;
	}

	create_dmaudiopath(&IID_IDirectMusicAudioPath, (void**)&pPath);
	set_audiopath_perf_pointer(pPath, iface);
	set_audiopath_dsound_buffer(pPath, buffer);
	set_audiopath_primary_dsound_buffer(pPath, primary_buffer);

	*ppNewPath = pPath;
	
	TRACE(" returning IDirectMusicAudioPath interface at %p.\n", *ppNewPath);

	return IDirectMusicAudioPath_Activate(*ppNewPath, fActivate);
}

static HRESULT WINAPI performance_SetDefaultAudioPath(IDirectMusicPerformance8 *iface, IDirectMusicAudioPath *pAudioPath)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %p): semi-stub\n", This, pAudioPath);

	if (This->pDefaultPath) {
		IDirectMusicAudioPath_Release(This->pDefaultPath);
		This->pDefaultPath = NULL;
	}
	This->pDefaultPath = pAudioPath;
	if (This->pDefaultPath) {
		IDirectMusicAudioPath_AddRef(This->pDefaultPath);
		set_audiopath_perf_pointer(This->pDefaultPath, iface);
	}

	return S_OK;
}

static HRESULT WINAPI performance_GetDefaultAudioPath(IDirectMusicPerformance8 *iface,
        IDirectMusicAudioPath **ppAudioPath)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %p): semi-stub (%p)\n", This, ppAudioPath, This->pDefaultPath);

	if (NULL != This->pDefaultPath) {
	  *ppAudioPath = This->pDefaultPath;
          IDirectMusicAudioPath_AddRef(*ppAudioPath);
        } else {
	  *ppAudioPath = NULL;
        }
	return S_OK;
}

static HRESULT WINAPI performance_GetParamEx(IDirectMusicPerformance8 *iface, REFGUID rguidType, DWORD dwTrackID,
        DWORD dwGroupBits, DWORD dwIndex, MUSIC_TIME mtTime, MUSIC_TIME *pmtNext, void *pParam)
{
        struct performance *This = impl_from_IDirectMusicPerformance8(iface);

	FIXME("(%p, %s, %ld, %ld, %ld, %ld, %p, %p): stub\n", This, debugstr_dmguid(rguidType), dwTrackID, dwGroupBits, dwIndex, mtTime, pmtNext, pParam);

	return S_OK;
}

static const IDirectMusicPerformance8Vtbl performance_vtbl =
{
    performance_QueryInterface,
    performance_AddRef,
    performance_Release,
    performance_Init,
    performance_PlaySegment,
    performance_Stop,
    performance_GetSegmentState,
    performance_SetPrepareTime,
    performance_GetPrepareTime,
    performance_SetBumperLength,
    performance_GetBumperLength,
    performance_SendPMsg,
    performance_MusicToReferenceTime,
    performance_ReferenceToMusicTime,
    performance_IsPlaying,
    performance_GetTime,
    performance_AllocPMsg,
    performance_FreePMsg,
    performance_GetGraph,
    performance_SetGraph,
    performance_SetNotificationHandle,
    performance_GetNotificationPMsg,
    performance_AddNotificationType,
    performance_RemoveNotificationType,
    performance_AddPort,
    performance_RemovePort,
    performance_AssignPChannelBlock,
    performance_AssignPChannel,
    performance_PChannelInfo,
    performance_DownloadInstrument,
    performance_Invalidate,
    performance_GetParam,
    performance_SetParam,
    performance_GetGlobalParam,
    performance_SetGlobalParam,
    performance_GetLatencyTime,
    performance_GetQueueTime,
    performance_AdjustTime,
    performance_CloseDown,
    performance_GetResolvedTime,
    performance_MIDIToMusic,
    performance_MusicToMIDI,
    performance_TimeToRhythm,
    performance_RhythmToTime,
    performance_InitAudio,
    performance_PlaySegmentEx,
    performance_StopEx,
    performance_ClonePMsg,
    performance_CreateAudioPath,
    performance_CreateStandardAudioPath,
    performance_SetDefaultAudioPath,
    performance_GetDefaultAudioPath,
    performance_GetParamEx,
};

static inline struct performance *impl_from_IDirectMusicGraph(IDirectMusicGraph *iface)
{
    return CONTAINING_RECORD(iface, struct performance, IDirectMusicGraph_iface);
}

static HRESULT WINAPI performance_graph_QueryInterface(IDirectMusicGraph *iface, REFIID riid, void **ret_iface)
{
    struct performance *This = impl_from_IDirectMusicGraph(iface);
    return IDirectMusicPerformance8_QueryInterface(&This->IDirectMusicPerformance8_iface, riid, ret_iface);
}

static ULONG WINAPI performance_graph_AddRef(IDirectMusicGraph *iface)
{
    struct performance *This = impl_from_IDirectMusicGraph(iface);
    return IDirectMusicPerformance8_AddRef(&This->IDirectMusicPerformance8_iface);
}

static ULONG WINAPI performance_graph_Release(IDirectMusicGraph *iface)
{
    struct performance *This = impl_from_IDirectMusicGraph(iface);
    return IDirectMusicPerformance8_Release(&This->IDirectMusicPerformance8_iface);
}

static HRESULT WINAPI performance_graph_StampPMsg(IDirectMusicGraph *iface, DMUS_PMSG *msg)
{
    struct performance *This = impl_from_IDirectMusicGraph(iface);
    HRESULT hr;

    TRACE("(%p, %p)\n", This, msg);

    if (!msg) return E_POINTER;

    /* FIXME: Implement segment and audio path graphs support */
    if (!This->pToolGraph) hr = DMUS_S_LAST_TOOL;
    else if (FAILED(hr = IDirectMusicGraph_StampPMsg(This->pToolGraph, msg))) return hr;

    if (msg->pGraph)
    {
        IDirectMusicTool_Release(msg->pGraph);
        msg->pGraph = NULL;
    }

    if (hr == DMUS_S_LAST_TOOL)
    {
        const DWORD delivery_flags = DMUS_PMSGF_TOOL_IMMEDIATE | DMUS_PMSGF_TOOL_QUEUE | DMUS_PMSGF_TOOL_ATTIME;
        msg->dwFlags &= ~delivery_flags;
        msg->dwFlags |= DMUS_PMSGF_TOOL_QUEUE;

        if (msg->pTool) IDirectMusicTool_Release(msg->pTool);
        msg->pTool = &This->IDirectMusicTool_iface;
        IDirectMusicTool_AddRef(msg->pTool);
        return S_OK;
    }

    if (SUCCEEDED(hr))
    {
        msg->pGraph = &This->IDirectMusicGraph_iface;
        IDirectMusicTool_AddRef(msg->pGraph);
    }

    return hr;
}

static HRESULT WINAPI performance_graph_InsertTool(IDirectMusicGraph *iface, IDirectMusicTool *tool,
        DWORD *channels, DWORD channels_count, LONG index)
{
    struct performance *This = impl_from_IDirectMusicGraph(iface);
    TRACE("(%p, %p, %p, %lu, %ld)\n", This, tool, channels, channels_count, index);
    return E_NOTIMPL;
}

static HRESULT WINAPI performance_graph_GetTool(IDirectMusicGraph *iface, DWORD index, IDirectMusicTool **tool)
{
    struct performance *This = impl_from_IDirectMusicGraph(iface);
    TRACE("(%p, %lu, %p)\n", This, index, tool);
    return E_NOTIMPL;
}

static HRESULT WINAPI performance_graph_RemoveTool(IDirectMusicGraph *iface, IDirectMusicTool *tool)
{
    struct performance *This = impl_from_IDirectMusicGraph(iface);
    TRACE("(%p, %p)\n", This, tool);
    return E_NOTIMPL;
}

static const IDirectMusicGraphVtbl performance_graph_vtbl =
{
    performance_graph_QueryInterface,
    performance_graph_AddRef,
    performance_graph_Release,
    performance_graph_StampPMsg,
    performance_graph_InsertTool,
    performance_graph_GetTool,
    performance_graph_RemoveTool,
};

static inline struct performance *impl_from_IDirectMusicTool(IDirectMusicTool *iface)
{
    return CONTAINING_RECORD(iface, struct performance, IDirectMusicTool_iface);
}

static HRESULT WINAPI performance_tool_QueryInterface(IDirectMusicTool *iface, REFIID riid, void **ret_iface)
{
    struct performance *This = impl_from_IDirectMusicTool(iface);
    return IDirectMusicPerformance8_QueryInterface(&This->IDirectMusicPerformance8_iface, riid, ret_iface);
}

static ULONG WINAPI performance_tool_AddRef(IDirectMusicTool *iface)
{
    struct performance *This = impl_from_IDirectMusicTool(iface);
    return IDirectMusicPerformance8_AddRef(&This->IDirectMusicPerformance8_iface);
}

static ULONG WINAPI performance_tool_Release(IDirectMusicTool *iface)
{
    struct performance *This = impl_from_IDirectMusicTool(iface);
    return IDirectMusicPerformance8_Release(&This->IDirectMusicPerformance8_iface);
}

static HRESULT WINAPI performance_tool_Init(IDirectMusicTool *iface, IDirectMusicGraph *graph)
{
    struct performance *This = impl_from_IDirectMusicTool(iface);
    TRACE("(%p, %p)\n", This, graph);
    return E_NOTIMPL;
}

static HRESULT WINAPI performance_tool_GetMsgDeliveryType(IDirectMusicTool *iface, DWORD *type)
{
    struct performance *This = impl_from_IDirectMusicTool(iface);
    TRACE("(%p, %p)\n", This, type);
    *type = DMUS_PMSGF_TOOL_IMMEDIATE;
    return S_OK;
}

static HRESULT WINAPI performance_tool_GetMediaTypeArraySize(IDirectMusicTool *iface, DWORD *size)
{
    struct performance *This = impl_from_IDirectMusicTool(iface);
    TRACE("(%p, %p)\n", This, size);
    *size = 0;
    return S_OK;
}

static HRESULT WINAPI performance_tool_GetMediaTypes(IDirectMusicTool *iface, DWORD **types, DWORD size)
{
    struct performance *This = impl_from_IDirectMusicTool(iface);
    TRACE("(%p, %p, %lu)\n", This, types, size);
    return E_NOTIMPL;
}

static HRESULT WINAPI performance_tool_ProcessPMsg(IDirectMusicTool *iface,
        IDirectMusicPerformance *performance, DMUS_PMSG *msg)
{
    struct performance *This = impl_from_IDirectMusicTool(iface);
    FIXME("(%p, %p, %p): stub\n", This, performance, msg);
    return E_NOTIMPL;
}

static HRESULT WINAPI performance_tool_Flush(IDirectMusicTool *iface,
        IDirectMusicPerformance *performance, DMUS_PMSG *msg, REFERENCE_TIME time)
{
    struct performance *This = impl_from_IDirectMusicTool(iface);
    FIXME("(%p, %p, %p, %I64d): stub\n", This, performance, msg, time);
    return E_NOTIMPL;
}

static const IDirectMusicToolVtbl performance_tool_vtbl =
{
    performance_tool_QueryInterface,
    performance_tool_AddRef,
    performance_tool_Release,
    performance_tool_Init,
    performance_tool_GetMsgDeliveryType,
    performance_tool_GetMediaTypeArraySize,
    performance_tool_GetMediaTypes,
    performance_tool_ProcessPMsg,
    performance_tool_Flush,
};

/* for ClassFactory */
HRESULT create_dmperformance(REFIID iid, void **ret_iface)
{
    struct performance *obj;
    HRESULT hr;

    TRACE("(%s, %p)\n", debugstr_guid(iid), ret_iface);

    *ret_iface = NULL;
    if (!(obj = calloc(1, sizeof(*obj)))) return E_OUTOFMEMORY;
    obj->IDirectMusicPerformance8_iface.lpVtbl = &performance_vtbl;
    obj->IDirectMusicGraph_iface.lpVtbl = &performance_graph_vtbl;
    obj->IDirectMusicTool_iface.lpVtbl = &performance_tool_vtbl;
    obj->ref = 1;

    obj->pDefaultPath = NULL;
    InitializeCriticalSection(&obj->safe);
    obj->safe.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": performance->safe");
    wine_rb_init(&obj->pchannels, pchannel_block_compare);

    obj->rtLatencyTime  = 100;  /* 100 ms TO FIX */
    obj->dwBumperLength =   50; /* 50 ms default */
    obj->dwPrepareTime  = 1000; /* 1000 ms default */

    hr = IDirectMusicPerformance8_QueryInterface(&obj->IDirectMusicPerformance8_iface, iid, ret_iface);
    IDirectMusicPerformance_Release(&obj->IDirectMusicPerformance8_iface);
    return hr;
}
