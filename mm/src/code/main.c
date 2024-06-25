#ifdef _WIN32
#include <Windows.h>
#include <stdio.h>
#endif

#include "audiomgr.h"
#include "fault.h"
#include "idle.h"
#include "irqmgr.h"
#include "padmgr.h"
#include "scheduler.h"
#include "CIC6105.h"
#include "stack.h"
#include "stackcheck.h"
#include "BenPort.h"
#include "intent-control-types.h"

// Variables are put before most headers as a hacky way to bypass bss reordering
OSMesgQueue sSerialEventQueue;
OSMesg sSerialMsgBuf[1];
uintptr_t gSegments[NUM_SEGMENTS];
SchedContext gSchedContext;
IrqMgrClient sIrqClient;
OSMesgQueue sIrqMgrMsgQueue;
OSMesg sIrqMgrMsgBuf[60];
OSThread gGraphThread;
STACK(sGraphStack, 0x1800);
STACK(sSchedStack, 0x600);
STACK(sAudioStack, 0x800);
STACK(sPadMgrStack, 0x500);
StackEntry sGraphStackInfo;
StackEntry sSchedStackInfo;
StackEntry sAudioStackInfo;
StackEntry sPadMgrStackInfo;
AudioMgr sAudioMgr;
static s32 sBssPad;
PadMgr gPadMgr;

#include "main.h"
#include "buffers.h"
#include "global.h"
#include "system_heap.h"
#include "z64thread.h"

s32 gScreenWidth = SCREEN_WIDTH;
s32 gScreenHeight = SCREEN_HEIGHT;
size_t gSystemHeapSize = 0;

void InitOTR();

#ifdef __GNUC__
#define SDL_main main
#endif

IntentControlDefinition* intentDefinitions = NULL;
uint16_t intentDefinitionCount = 0;

ArbitraryItemEquipButton *arbitraryItemEquipButtonDefinitions = NULL;
size_t arbitraryItemEquipButtonDefinitionCount = 0;

uint16_t itemIndex = 0;
uint8_t canTakeAssignment(ArbitraryItemEquipButton* self, ItemId item){
    uint8_t allow = (itemIndex % 10) == (self->id - 1);
    
    return allow;
}
uint8_t assignmentTriggeredTrue(ArbitraryItemEquipButton* self, Input* input){
    if(CHECK_BTN_ALL(input->press.button, BTN_A)){
        itemIndex++;
        return true;
    }
    return false;
}

void SDL_main(int argc, char** argv /* void* arg*/) {
    intptr_t fb;
    intptr_t sysHeap;
    s32 exit;
    s16* msg;

    IntentControlDefinition d[] = {
        {INTENT_CONTROL_ROLL, "Roll"}, 
        {INTENT_CONTROL_JUMP, "Jump"},
        {INTENT_CONTROL_TALK, "Talk"},
        {INTENT_CONTROL_FIRE_BOW, "Fire Bow"},
        {INTENT_CONTROL_FIRE_HOOKSHOT, "Fire Hookshot"},
        {INTENT_HOTSWAP_ITEM_RIGHT, "Swap Item Right"},
        {INTENT_HOTSWAP_ITEM_LEFT, "Swap Item Left"},
        {INTENT_USE_ITEM, "Use Item"}
    };
    intentDefinitions = d;
    intentDefinitionCount = sizeof(d) / sizeof(IntentControlDefinition);

    ArbitraryItemEquipButton e[10];
    uint16_t width = 27, height = 27;
    for(uint8_t i = 0; i < 10; i++){
            e[i].id = i + 1;
            e[i].assignedItem = ITEM_HOOKSHOT + i;
            e[i].rectLeft = 0;
            e[i].rectTop = (height * i);
            e[i].rectWidth = width;
            e[i].rectHeight = height;
            e[i].dsdx = 620;
            e[i].dtdy = 620;
            e[i].r = i % 3 == 0 ? 255 : 0;
            e[i].g = i % 3 == 1 ? 255 : 0;
            e[i].b = i % 3 == 2 ? 255 : 0;
            e[i].a = 255;
            e[i].canTakeAssignment = canTakeAssignment;
            e[i].assignmentTriggered = assignmentTriggeredTrue;
    }
    arbitraryItemEquipButtonDefinitions = e;
    arbitraryItemEquipButtonDefinitionCount = sizeof(e) / sizeof(ArbitraryItemEquipButton);

// Attach console for windows so we can conditionally display it when running the extractor
#ifdef _WIN32
    AllocConsole();
    (void)freopen("CONIN$", "r", stdin);
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
#ifndef _DEBUG
    ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
#endif // _WIN32

    InitOTR();
    Heaps_Alloc();

    gScreenWidth = SCREEN_WIDTH;
    gScreenHeight = SCREEN_HEIGHT;

    Nmi_Init();
    Fault_Init();
    Check_RegionIsSupported();
    Check_ExpansionPak();
    sysHeap = gSystemHeap;
    // fb = 0x80780000;
    // gSystemHeapSize = fb - sysHeap;
    SystemHeap_Init(sysHeap, SYSTEM_HEAP_SIZE);

    Regs_Init();

    R_ENABLE_ARENA_DBG = 0;

    osCreateMesgQueue(&sSerialEventQueue, sSerialMsgBuf, ARRAY_COUNT(sSerialMsgBuf));
    osSetEventMesg(OS_EVENT_SI, &sSerialEventQueue, OS_MESG_PTR(NULL));

    osCreateMesgQueue(&sIrqMgrMsgQueue, sIrqMgrMsgBuf, ARRAY_COUNT(sIrqMgrMsgBuf));
    PadMgr_Init(&sSerialEventQueue, &gIrqMgr, Z_THREAD_ID_PADMGR, Z_PRIORITY_PADMGR, STACK_TOP(sPadMgrStack));

    AudioMgr_Init(&sAudioMgr, STACK_TOP(sAudioStack), Z_PRIORITY_AUDIOMGR, Z_THREAD_ID_AUDIOMGR, &gSchedContext,
                  &gIrqMgr);
#if 0
    StackCheck_Init(&sSchedStackInfo, sSchedStack, STACK_TOP(sSchedStack), 0, 0x100, "sched");
    Sched_Init(&gSchedContext, STACK_TOP(sSchedStack), Z_PRIORITY_SCHED, gViConfigModeType, 1, &gIrqMgr);

    CIC6105_AddRomInfoFaultPage();

    IrqMgr_AddClient(&gIrqMgr, &sIrqClient, &sIrqMgrMsgQueue);

    StackCheck_Init(&sAudioStackInfo, sAudioStack, STACK_TOP(sAudioStack), 0, 0x100, "audio");
    AudioMgr_Init(&sAudioMgr, STACK_TOP(sAudioStack), Z_PRIORITY_AUDIOMGR, Z_THREAD_ID_AUDIOMGR, &gSchedContext,
                  &gIrqMgr);

    StackCheck_Init(&sPadMgrStackInfo, sPadMgrStack, STACK_TOP(sPadMgrStack), 0, 0x100, "padmgr");

    AudioMgr_Unlock(&sAudioMgr);
    StackCheck_Init(&sGraphStackInfo, sGraphStack, STACK_TOP(sGraphStack), 0, 0x100, "graph");
    osCreateThread(&gGraphThread, Z_THREAD_ID_GRAPH, Graph_ThreadEntry, NULL, STACK_TOP(sGraphStack), Z_PRIORITY_GRAPH);
    osStartThread(&gGraphThread);
#endif

    Graph_ThreadEntry(0);

    exit = false;

    while (!exit) {
        msg = NULL;
        osRecvMesg(&sIrqMgrMsgQueue, (OSMesg*)&msg, OS_MESG_BLOCK);
        if (msg == NULL) {
            break;
        }

        switch (*msg) {
            case OS_SC_PRE_NMI_MSG:
                Nmi_SetPrenmiStart();
                break;

            case OS_SC_NMI_MSG:
                exit = true;
                break;
        }
    }

    IrqMgr_RemoveClient(&gIrqMgr, &sIrqClient);
    osDestroyThread(&gGraphThread);

    DeinitOTR();

#ifdef _WIN32
    FreeConsole();
#endif
}
