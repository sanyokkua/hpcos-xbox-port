/**
 * Manual function overrides and ICALL diagnostics -- Harry Potter and the
 * Chamber of Secrets (Xbox, NTSC-US). The XDK glue (CRT memmove, D3D fence wait
 * and interrupt handlers, DSOUND DSP post, AC'97 reset) comes from the NFSU1
 * port (nfsu1-xbox-port), its addresses moved to HPCOS's by masked byte match.
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

/* The generated register model (g_eax/g_esp are thread-local there) and the
 * XBOX_PTR/MEM32 accessors; the register names below are its macros. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

extern ptrdiff_t g_xbox_mem_offset;

/* ── CRT memmove/memcpy ───────────────────────────────────────
 *
 * Two copies of the MSVC CRT memmove are linked (0x00127A00 and 0x002A9450;
 * memcpy is the same body). Their tail and backward-copy paths dispatch
 * through UnwindDown-style vectors indexed with a *negative* register -- the
 * table address in the instruction is the end of the table -- which the
 * lifter's forward table reader cannot follow, so those jumps were lowered
 * to indirect tail calls into the middle of the function and failed to
 * resolve: every copy whose length was not a multiple of 4, and every
 * overlapping backward copy, silently lost its tail.
 *
 * Replaced by the host memmove. cdecl (dst, src, count), returns dst, and
 * esi/edi are preserved, as the original restores them. Generated bodies are
 * skipped by tools.recomp --exclude-manual reading this file. */
static void crt_memmove(void)
{
    uint32_t dst = MEM32(esp + 4);
    uint32_t src = MEM32(esp + 8);
    uint32_t n   = MEM32(esp + 12);

    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    eax = dst;
    esp += 4;   /* return address; cdecl, the caller pops the arguments */
}

void sub_00127A00(void) { crt_memmove(); }

/* ── D3D fence wait, D3D_BlockOnTime (0x001359D0) ────────────
 *
 * stdcall (fence, flags), ret 8. HPCOS's XDK 5659 D3D keeps its device at
 * 0x001A1400, reached through the global at 0x00140888 (found by
 * signature from HPCOS/NFSU2; HP-PS-Xbox-Recomp DEVELOPING.md 6.1 gives PAL 0x1A13D8, +0x20 on NTSC):
 *   +0x2C  write sequence, bumped as fences are inserted
 *   +0x30  pointer to the semaphore the GPU releases as it completes work
 * and this spins until *(+0x30) catches up with the fence it was asked for.
 *
 * Without the pushbuffer executor nothing ever releases the semaphore, so the
 * GPU is reported caught up. With it (RECOMP_PB_EXEC) that would let D3D
 * reuse ring space and vertex memory the executor has not read yet (see the
 * toolkit's d3d8ltcg-device-context.md), so instead the real semaphore is
 * handed to the executor once and the original wait runs against it. */
#define HPCOS_D3D_DEVICE_PTR 0x00140888u
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
extern void sub_001359D0_gen(void);

void sub_001359D0(void)
{
    static int exec = -1;
    uint32_t dev = MEM32(HPCOS_D3D_DEVICE_PTR);
    uint32_t sem = dev ? MEM32(dev + 0x30) : 0;

    if (exec < 0)
        exec = getenv("RECOMP_PB_EXEC") != NULL;
    if (exec) {
        static uint32_t registered;
        if (sem && sem != registered) {
            nv2a_pb_set_semaphore_target(sem);
            fprintf(stderr, "[D3D] GPU semaphore at 0x%08X\n", sem);
            registered = sem;
        }
        if (0 /* NFSU2 main-loop return addresses */) {
            /* The main loop (sub_000AEA90) calls BlockOnFence on the fence
             * of the frame it just built, right before Present: the whole
             * frame has to be through the GPU before the next one starts,
             * so the game and the pushbuffer executor take turns instead
             * of overlapping (Eden race: game waited ~50% of the time,
             * executor ~20% idle). RECOMP_FRAME_LAG=1 waits for the
             * previous frame's fence instead: one frame in flight, as on a
             * PC. Race frames checked on Linux, no corruption. */
            static int lag = -1;
            static uint32_t prev;
            if (lag < 0) {
                const char *e = getenv("RECOMP_FRAME_LAG");
                lag = e && *e == '1';
                fprintf(stderr, "[D3D] frame fence: %s\n",
                        lag ? "previous frame (RECOMP_FRAME_LAG=1)" : "this frame");
            }
            if (lag) {
                uint32_t mine = MEM32(esp + 4);
                if (prev)
                    MEM32(esp + 4) = prev;
                prev = mine;
            }
        }
        sub_001359D0_gen();
        return;
    }
    if (sem)
        MEM32(sem) = MEM32(dev + 0x2C);     /* "GPU" is always caught up */
    eax = 0;
    esp += 4 + 8;                           /* return address, 2 stdcall args */
}

/* ── D3D interrupt handlers at DISPATCH_LEVEL ────────────────
 *
 * D3D services the GPU from its DPC (0x001398D0) and, with the interrupt
 * masked, from its own busy-waits (BlockUntilIdle and friends) on the game
 * thread: both call the vblank handler (0x00139080) and the PGRAPH handler
 * (0x00139690), which queues flips. On the Xbox the two never interleave --
 * one is a DPC, the other runs with the interrupt off. Here the DPC runs on
 * the kernel timer thread, and lifted code hands over the guest lock at any
 * function entry, so the vblank count could move between D3D computing a
 * flip's target vblank and checking it. D3D retires a flip only when the
 * count *equals* its target, so that flip stayed pending forever and
 * PersistDisplay, which waits for pending flips, never returned: the hang
 * starting a Quick Race or Career. Run at DISPATCH_LEVEL (the dispatch lock,
 * kernel_hal.c), neither can be interrupted by the other.
 *
 * Raising blocks on the lock that the timer thread holds while it waits for
 * the guest lock, so the guest lock is let go for the raise, as a kernel
 * call would; already at DISPATCH (inside the DPC) there is nothing to take. */
extern uint8_t xbox_KfRaiseIrql(uint8_t irql);
extern void xbox_KfLowerIrql(uint8_t irql);
extern uint8_t xbox_CurrentIrql(void);
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);
extern void xbox_Nv2aVblankTaken(void);

static uint8_t d3d_isr_enter(void)
{
    uint8_t old = xbox_CurrentIrql();

    if (old < 2) {
        int d = xbox_gil_suspend();
        old = xbox_KfRaiseIrql(2);
        xbox_gil_resume(d);
    }
    return old;
}

static void d3d_isr_leave(uint8_t old)
{
    if (old < 2)
        xbox_KfLowerIrql(old);
}

/* Flips retired by a handler call: D3D's retire index, at +0x1BC of the
 * block both handlers take in ecx (their own reads index the pending-flip
 * slots from it). The pushbuffer executor's FLIP_STALL waits on these, as
 * PGRAPH waits on the retire's PGRAPH_INCREMENT write. */
extern void nv2a_pb_flip_retired(unsigned n);

/* Vblank handler, thiscall. It ends by writing PCRTC_INTR and spinning until
 * PMC_INTR bit 24 drops. Its callers (the DPC, D3D's busy-waits) have read
 * the bits before calling it, so this is the vblank being taken: clear them
 * now, and the spin ends at once instead of holding the guest lock until the
 * runtime's ack thread comes round. */
extern void sub_00139080_gen(void);

void sub_00139080(void)
{
    uint32_t blk = ecx;
    uint8_t old = d3d_isr_enter();
    /* Read under the dispatch lock: raising IRQL can drop the guest lock and
     * let the other handler retire a flip, which we would count again. */
    uint32_t head = MEM32(blk + 0x1BC);

    xbox_Nv2aVblankTaken();
    sub_00139080_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);
    d3d_isr_leave(old);
}

/* PGRAPH handler, thiscall, ecx = the device's hardware block (+0 =
 * 0xFD000000). Reads a software-method trap from PGRAPH_INTR / NSOURCE /
 * TRAPPED_ADDR / TRAPPED_DATA, acknowledges it by writing PGRAPH_INTR back,
 * and acts on it: NOP(5) sets the event BlockOnTime sleeps on, a flip trap
 * queues the flip (sub_002F2080). The acknowledge is write-1-to-clear on
 * hardware and a no-op on RAM. Left pending, every turn of a busy-wait took
 * the same trap again (tens of thousands of flips a second, until the flip
 * targets were millions of vblanks ahead); guessed from the handler's
 * PGRAPH_FIFO writes, a trap posted during a call was sometimes dropped (the
 * fence event never set). So the executor posts traps under a lock held here
 * around the call, and a call that found one reports it taken, which clears
 * it (nv2a_pb_exec.c). */
extern void sub_00139690_gen(void);
extern void nv2a_pb_trap_lock(void);
/* The frame-rate cap. Present turns D3D's presentation interval (1, 2, 4
 * vblanks, bit 31 = flip at once when late) into a NOP trap word,
 * (bits << 5) | 1 -- bits 0..2 = 1/2/3 for 1/2/4 vblanks, bit 3 =
 * immediate -- and the flip queue schedules the flip that many vblanks
 * after the last one (same D3D code and trap format as NFSU1/NFSU2). HPCOS
 * keeps the title's interval by default: unlike NFSU, parts of it step per
 * frame (HP-PS-Xbox-Recomp had to fix the aiming circle and the Lumos moths
 * at other rates). HPCOS_FPS=60 rewrites the interval to 1 as the trap is
 * taken, so a frame ready in time shows on the next vblank. */
static int fps_cap_30(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("HPCOS_FPS");
        v = !(e && atoi(e) == 60);
        fprintf(stderr, "[fps] flip interval: %s\n", v ? "the title's (HPCOS_FPS=60 for 1 vblank)" : "1 vblank (HPCOS_FPS=60)");
    }
    return v;
}
extern void nv2a_pb_trap_unlock(void);
extern void nv2a_pb_trap_taken(void);

void sub_00139690(void)
{
    volatile uint32_t *nv = (volatile uint32_t *)XBOX_PTR(0xFD000000u);
    uint32_t blk = ecx;
    uint8_t old = d3d_isr_enter();
    uint32_t nsource, head;

    nv2a_pb_trap_lock();
    head = MEM32(blk + 0x1BC);   /* under the locks, as in sub_00139080 */
    nsource = nv[0x400108 / 4];
    if (nsource && (nv[0x400704 / 4] & 0xFFFFu) == 0x0100u && !fps_cap_30()) {
        uint32_t data = nv[0x400708 / 4];
        if ((data & 0x1Fu) == 1u && ((data >> 5) & 7u) > 1u) {
            static int told;
            nv[0x400708 / 4] = (data & ~(7u << 5)) | (1u << 5);   /* flip: 1 vblank */
            if (!told++)
                fprintf(stderr, "[fps] flip interval %u -> 1 vblank (uncapped)\n", (data >> 5) & 7u);
        }
    }
    {   /* HPCOS_TRAP_LOG=1: each PGRAPH handler call: source, trapped method and data */
        static int lg = -1;
        if (lg < 0) { const char *e = getenv("HPCOS_TRAP_LOG"); lg = e && *e == '1'; }
        if (lg) fprintf(stderr, "[trap] PGRAPH handler: NSOURCE %08X INTR %08X ADDR %08X DATA %08X\n",
                        nsource, nv[0x400100 / 4], nv[0x400704 / 4], nv[0x400708 / 4]);
    }
    sub_00139690_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);   /* a flip queued on its vblank retires at once */
    if (nsource)
        nv2a_pb_trap_taken();
    nv2a_pb_trap_unlock();
    d3d_isr_leave(old);
}

/* ── DirectSound DSP command post (0x001465D2) ───────────────
 *
 * thiscall, ecx = the DSP-side object. Copies a command block into the GP
 * DSP's scratch area, stores the command code at scratch + 0x810, and spins
 * until the DSP zeroes it (this loop does re-read memory). The scratch block
 * is ***(this + 8 + 0x10), the same chain the runtime's RECOMP_DSP_ACK notes
 * describe. There is no DSP, so register the word with the runtime's
 * completion list before running the original body. */
extern int xbox_ApuDspAckWord(uint32_t va);
extern void sub_001465D2_gen(void);

void sub_001465D2(void)
{
    uint32_t obj = MEM32(ecx + 8);
    uint32_t blk = obj ? MEM32(obj + 0x10) : 0;
    uint32_t scratch = blk ? MEM32(blk) : 0;

    if (scratch)
        xbox_ApuDspAckWord(scratch + 0x810);
    sub_001465D2_gen();
}

/* ── DirectSound AC'97 channel reset (0x0014CBAD) ────────────
 *
 * thiscall, ecx = channel object. Sets RR (bit 1) in the channel's NABM
 * control byte, then waits for the controller to clear it -- except MSVC
 * hoisted the load out of the loop, so the original reads the byte once and
 * spins on the register copy forever unless the bit is already clear at that
 * one read:
 *
 *     mov  byte [eax+0xFEC0010B], 2
 *     mov  cl, [eax+0xFEC0010B]
 *     and  cl, 2
 *   L: test cl, cl
 *     jne  L
 *
 * On hardware the reset has completed by then. The Windows runtime answers it
 * with a write trap on the NABM page; there is no fault handling on POSIX or
 * Switch, so this is the same function with the reset completing at once.
 * Everything else -- the lock taken through sub_00144510/sub_00144543, the
 * buffer-descriptor base store, the 0xFEC0017C write for channel 1 and the
 * sub_0014C9B7 re-arm -- is as the original does it. */
void sub_0014CBAD(void)
{
    uint32_t ebp_saved = g_ebp, frame, chan, nabm;

    PUSH32(esp, ebp_saved);
    frame = esp;
    g_ebp = frame;
    g_seh_ebp = frame;
    esp -= 8;
    MEM32(frame - 4) = 0;
    PUSH32(esp, esi);
    esi = ecx;
    chan = esi;

    ecx = frame - 8;
    PUSH32(esp, 0x0014CBC1u); RECOMP_ABI_CALL(0x00144510u, sub_00144510);

    nabm = MEM32(MEM32(chan) * 4 + 0x0014DB74u);
    MEM8(nabm + 0xFEC0010Bu) = 2;
    MEM8(nabm + 0xFEC0010Bu) &= (uint8_t)~2u;   /* reset complete */
    eax = nabm;
    SET_LO8(ecx, 0);

    MEM32(nabm + 0xFEC00100u) = MEM32(chan + 0x1C);
    if (MEM32(chan) == 1)
        MEM32(0xFEC0017Cu) = MEM32(chan + 0x28);

    PUSH32(esp, 1);
    PUSH32(esp, 1);
    ecx = chan;
    PUSH32(esp, MEM8(chan + 0x24));
    PUSH32(esp, MEM8(chan + 0x25));
    eax = MEM8(chan + 0x25);
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x0014CC14u); RECOMP_ABI_CALL(0x0014C9B7u, sub_0014C9B7);

    ecx = frame - 8;
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x0014CC1Cu); RECOMP_ABI_CALL(0x00144543u, sub_00144543);

    POP32(esp, esi);
    esp = frame;
    POP32(esp, ebp_saved);
    g_ebp = ebp_saved;
    esp += 4;
}

/* ── Controllers: XAPI's XInput answered from the host pad ─────────────
 *
 * From MiddleDucky/HP-PS-Xbox-Recomp's port/src/hle_input.c (MIT; itself from
 * SCDA-XBERecomp's xapi.cpp), addresses moved PAL -> NTSC (+0x20: XPP follows
 * .text, 32 bytes longer on this disc). Over the emulated OHCI the pad
 * enumerated up to SET_CONFIGURATION and HPCOS never opened it; HP-PS also found
 * the stick arriving late that way. XInitDevices, XInputOpen/Close,
 * XInputGetCapabilities, XInputGetState and XInputSetState are answered here;
 * XGetDevices and XGetDeviceChanges stay XAPI's and read the device-type record
 * kept below. The state is the USB pad's report (usb_gamepad_report): host
 * pad, keyboard, RECOMP_PAD_SCRIPT and the overlay's neutral pad all apply.
 * HPCOS_XINPUT_USB=1 keeps XAPI's own bodies on the emulated USB. */
#include <pthread.h>
#define XPP_GAMEPAD_TYPE 0x00176294u  /* g_DeviceType_Gamepad: Current, Change, Previous connected */
#define XPP_TYPES_BEGIN  0x00176264u  /* the linked device types, begin .. end */
#define XPP_TYPES_END    0x0017626Cu
#define XI_HANDLE_BASE   0x5ADE0010u  /* opaque handles, never dereferenced by the title */
#define XI_MAX_HANDLES   32
#define XI_PORTS         2            /* the runtime's pads */
#define ARG(i)           MEM32(esp + 4u + 4u * (i))

extern int usb_gamepad_connected(int dev);
extern int usb_gamepad_report(int dev, uint8_t *out, int max);
extern void usb_gamepad_output(int dev, const uint8_t *data, int len);
extern void xbox_GuestSetEvent(uint32_t token);

static pthread_mutex_t s_xi_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { int used; uint32_t port, packet; uint8_t last[16]; } s_xi[XI_MAX_HANDLES];

static int xi_hle(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("HPCOS_XINPUT_USB");
        on = !(e && *e == '1');
        fprintf(stderr, "[input] XInput %s\n", on ? "from the host pad (direct)" : "through the emulated USB");
    }
    return on;
}

static void xi_update_ports(void)
{
    uint32_t cur = 0, old;
    int p;
    for (p = 0; p < XI_PORTS; p++)
        if (usb_gamepad_connected(p))
            cur |= 1u << p;
    pthread_mutex_lock(&s_xi_lock);
    old = MEM32(XPP_GAMEPAD_TYPE);
    if (cur != old) {
        MEM32(XPP_GAMEPAD_TYPE + 4) |= cur ^ old;
        MEM32(XPP_GAMEPAD_TYPE) = cur;
        fprintf(stderr, "[input] controller ports now %X (was %X)\n", cur, old);
    }
    pthread_mutex_unlock(&s_xi_lock);
}

static void *xi_poller(void *unused)
{
    (void)unused;
    for (;;) {
        usleep(250000);
        xi_update_ports();
    }
    return NULL;
}

static int xi_index(uint32_t h)
{
    uint32_t i = (h - XI_HANDLE_BASE) / 16u;
    if (h < XI_HANDLE_BASE || (h - XI_HANDLE_BASE) % 16u || i >= XI_MAX_HANDLES || !s_xi[i].used)
        return -1;
    return (int)i;
}

/* XInitDevices(dwPreallocTypeCount, PreallocTypes). The pads are reported by
 * the poller 250 ms in, as insertions, as on the console (HP-PS: a pad already
 * present here was consumed by a boot-time XGetDevices and never "arrived"). */
extern void sub_00176729_gen(void);
void sub_00176729(void)
{
    static int poller;
    uint32_t t;
    int i;
    if (!xi_hle()) { sub_00176729_gen(); return; }
    for (t = XPP_TYPES_BEGIN; t < XPP_TYPES_END; t += 4) {
        uint32_t info = MEM32(t), type = info ? MEM32(info + 4) : 0;
        if (type)
            for (i = 0; i < 3; i++) MEM32(type + 4u * i) = 0;
    }
    for (i = 0; i < 3; i++) MEM32(XPP_GAMEPAD_TYPE + 4u * i) = 0;
    if (!poller) {
        pthread_t th;
        poller = 1;
        pthread_create(&th, NULL, xi_poller, NULL);
        pthread_detach(th);
    }
    esp += 4 + 8;
}

/* XInputOpen(DeviceType, dwPort, dwSlot, pPollingParameters) -> HANDLE */
extern void sub_001771FB_gen(void);
void sub_001771FB(void)
{
    uint32_t type = ARG(0), port = ARG(1);
    int i;
    if (!xi_hle()) { sub_001771FB_gen(); return; }
    eax = 0;
    if (type == XPP_GAMEPAD_TYPE && port < XI_PORTS)
        for (i = 0; i < XI_MAX_HANDLES; i++)
            if (!s_xi[i].used) {
                memset(&s_xi[i], 0, sizeof s_xi[i]);
                s_xi[i].used = 1;
                s_xi[i].port = port;
                eax = XI_HANDLE_BASE + 16u * (uint32_t)i;
                break;
            }
    fprintf(stderr, "[input] XInputOpen(port %u) = %08X\n", port, eax);
    esp += 4 + 16;
}

/* XInputClose(hDevice) */
extern void sub_00177251_gen(void);
void sub_00177251(void)
{
    int i;
    if (!xi_hle()) { sub_00177251_gen(); return; }
    i = xi_index(ARG(0));
    if (i >= 0) s_xi[i].used = 0;
    esp += 4 + 4;
}

/* XInputGetCapabilities(hDevice, pCapabilities): SubType, Reserved, then the
 * gamepad's In (every button and axis) and Out (both motors). */
extern void sub_0017725D_gen(void);
void sub_0017725D(void)
{
    uint32_t caps = ARG(1);
    int i = xi_hle() ? xi_index(ARG(0)) : -2, k;
    if (i == -2) { sub_0017725D_gen(); return; }
    if (i < 0) {
        eax = 6;                                    /* ERROR_INVALID_HANDLE */
    } else if (!usb_gamepad_connected((int)s_xi[i].port)) {
        eax = 1167;                                 /* ERROR_DEVICE_NOT_CONNECTED */
    } else {
        MEM8(caps) = 1;                             /* XINPUT_DEVSUBTYPE_GC_GAMEPAD */
        MEM8(caps + 1) = 0;
        MEM16(caps + 2) = 0;
        MEM16(caps + 4) = 0x00FF;
        for (k = 0; k < 8; k++) MEM8(caps + 6 + k) = 0xFF;
        for (k = 0; k < 4; k++) MEM16(caps + 14 + 2 * k) = 0xFFFF;
        MEM16(caps + 22) = 0xFFFF;
        MEM16(caps + 24) = 0xFFFF;
        eax = 0;
    }
    esp += 4 + 8;
}

/* ── Cutscene skip (overlay: Tweaks, HPCOS_SKIP_CUTSCENES=1) ────────────────
 * After MiddleDucky's HP2-Recompiled (MIT, port/src/hle_skip.c): pressing Start
 * or Back while a cutscene plays (the camera / control mode [0x50C2E4] is 7,
 * set as a cutscene takes the camera) runs it to its own end as fast as the
 * CPU allows (~30x): the title's frame loop renders and swaps only every
 * 250 ms (sub_00035190 / sub_00134D70 below), the vblank and the APU's clock
 * run FF_SCALE x, the sound is muted. Nothing is left out of the story:
 * scripts, speech and camera run as they would, only faster, so whatever the
 * cutscene sets up is set up. It stops at the first pass out of mode 7 and goes
 * on by itself into a next part that starts within 2.5 s. The press that
 * starts it is not passed to the game. HPCOS_FF_LOG=1 logs every Start/Back
 * press with the camera mode, and the passes a second. (HP2-Recompiled leaves
 * out sub_00032980; its caller sub_00032B70 loops until it returns 0.) */
#define CAMERA_MODE 0x0050C2E4u
#define FF_SCALE 6       /* the vblank's and the APU's speed-up (above ~10x the timer thread starves the game) */
extern volatile int xbox_vblank_turbo, nv2a_mtl_turbo;
extern void xa2_set_mute(int on);
static volatile int s_ff;
static uint64_t ff_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static int ff_enabled(void)
{
    const char *e = getenv("HPCOS_SKIP_CUTSCENES");
    return !(e && *e == '0');
}
static void ff_set(int on)
{
    if (s_ff == on) return;
    s_ff = on;
    {
        const char *e = getenv("HPCOS_FF_SCALE");
        int k = e && atoi(e) >= 2 && atoi(e) <= 64 ? atoi(e) : FF_SCALE;
        xbox_vblank_turbo = on ? k : 1;
    }
    nv2a_mtl_turbo = on ? 2 : 0;    /* 2: every frame drawn, only those with the 3D pass shown */
    xa2_set_mute(on);
    fprintf(stderr, "[skip] cutscene fast-forward %s\n", on ? "on" : "off");
}
/* Dev: HPCOS_WARP=<level>[:start] loads that level once, 3 s into play (camera mode 0),
 * the way the pause menu's debug level selector does (HP-COS's trainer, 0x4F268:
 * sub_0004AD50(level), the start point at 0x527DFC and 0x1EF704). Levels by the table
 * at 0x1E3DE8: 0 The Burrow, 4 Borgin & Burkes, 5 Diagon Alley, 30 Gryffindor Common
 * Room, ... (HP-COS's trainer.c). For tests only: no menu row. */
extern void sub_0004AD50(void);
static uint64_t s_warp_at;                  /* when dev_warp ran */
static void dev_warp(void)
{
    static int done, level = -2, start = -1;
    static uint64_t since;
    if (done) return;
    if (level == -2) {
        const char *e = getenv("HPCOS_WARP");
        level = e && *e ? atoi(e) : -1;
        if (e && strchr(e, ':')) start = atoi(strchr(e, ':') + 1);
        if (level < 0) { done = 1; return; }
    }
    {
        uint32_t ent = MEM32(0x00530408u);  /* in a level: Harry's entity exists */
        if (MEM32(CAMERA_MODE) != 0u || !ent || !MEM32(ent + 0xAC)) { since = 0; return; }
    }
    if (!since) { since = ff_now_ms(); return; }
    if (ff_now_ms() - since < 3000) return;
    done = 1;
    {
        uint32_t s_esp = esp, s_ebx = ebx, s_esi = esi, s_edi = edi, s_ebp = g_ebp;
        esp -= 4; MEM32(esp) = (uint32_t)level;
        esp -= 4; MEM32(esp) = 0;               /* return address */
        sub_0004AD50();
        esp = s_esp; ebx = s_ebx; esi = s_esi; edi = s_edi; g_ebp = s_ebp;
    }
    MEM16(0x00527DFCu) = (uint16_t)start;
    MEM32(0x001EF704u) = (uint32_t)start;
    s_warp_at = ff_now_ms();
    fprintf(stderr, "[warp] level %d, start %d\n", level, start);
}

/* Dev: HPCOS_SAVE_NOW=<secs> asks for a save once, that long into play, as a SAVE GAME
 * scroll does (its handler 0x54DB0 tail-jumps to sub_0004DD30: copy the game state into
 * the slot, push the save-game mode). Tests the save screen and the write without
 * walking to a scroll. */
extern void sub_0004DD30(void);
static void dev_save_now(void)
{
    static int done, secs = -2;
    static uint64_t since;
    if (done) return;
    if (secs == -2) {
        const char *e = getenv("HPCOS_SAVE_NOW");
        secs = e && *e ? atoi(e) : -1;
        if (secs < 0) { done = 1; return; }
    }
    {
        uint32_t ent = MEM32(0x00530408u);
        if (MEM32(CAMERA_MODE) != 0u || !ent || !MEM32(ent + 0xAC)) { since = 0; return; }
    }
    if (!since) { since = ff_now_ms(); return; }
    if (ff_now_ms() - since < (uint64_t)secs * 1000u) return;
    done = 1;
    {
        uint32_t s_esp = esp, s_ebx = ebx, s_esi = esi, s_edi = edi, s_ebp = g_ebp;
        esp -= 4; MEM32(esp) = 0;               /* return address */
        sub_0004DD30();
        esp = s_esp; ebx = s_ebx; esi = s_esi; edi = s_edi; g_ebp = s_ebp;
    }
    fprintf(stderr, "[save] dev: save requested (slot %d)\n", (int)MEM32(0x0051DF64u));
}

/* Dev: Harry's update (sub_00053710, __thiscall: ecx = Harry, as HP-COS's trainer). His
 * object is [[0x530408] + 0xAC], his place at +0x14/+0x18/+0x1C. HPCOS_POS_LOG=1 logs it twice a second; HPCOS_TP=x,y,z
 * puts him there once, 2 s after a warp (or 6 s into play without one). */
static uint32_t s_harry;
static uint64_t s_pad_t0;                   /* the first pad read: the pad script's clock */
extern void sub_00053710_gen(void);
void sub_00053710(void)
{
    static int log = -1, ntp = -1, itp;
    static float tpv[32][2];
    static uint64_t last, since;
    {
        uint32_t ent = MEM32(0x00530408u);      /* the player entity; its object at +0xAC (HP-COS) */
        s_harry = ent ? MEM32(ent + 0xAC) : 0;
    }
    if (log < 0) { const char *e = getenv("HPCOS_POS_LOG"); log = e && *e == '1'; }
    if (ntp < 0) {                              /* HPCOS_TP="x,z;x,z;..." */
        const char *e = getenv("HPCOS_TP");
        ntp = 0;
        while (e && *e && ntp < 32 && sscanf(e, "%f,%f", &tpv[ntp][0], &tpv[ntp][1]) == 2) {
            ntp++;
            e = strchr(e, ';');
            if (e) e++;
        }
    }
    if (s_harry && (log || itp < ntp)) {
        uint32_t o = s_harry;
        uint64_t now = ff_now_ms();
        if (log && now - last >= 500) {
            last = now;
            fprintf(stderr, "[pos] %.2f %.2f %.2f\n", MEMF(o + 0x14), MEMF(o + 0x18), MEMF(o + 0x1C));
        }
        if (itp < ntp) {
            const char *w = getenv("HPCOS_WARP");
            if (!since) {                       /* 20 s after the warp (or 3 s into play), then 1.5 s each */
                if (!w) since = now + 3000;
                else if (s_warp_at) since = s_warp_at + 20000;
            } else if (now >= since + 1500u * (uint64_t)itp) {
                MEMF(o + 0x14) = tpv[itp][0]; MEMF(o + 0x1C) = tpv[itp][1];
                fprintf(stderr, "[tp] %d: %.2f %.2f at %.1f s\n", itp, tpv[itp][0], tpv[itp][1], (now - s_pad_t0) / 1000.0);
                itp++;
            }
        }
    }
    sub_00053710_gen();
}

/* Port 0's buttons byte as read; returns the byte the game gets. */
static uint8_t cutscene_ff_pad(uint8_t buttons)
{
    static uint64_t ff_since, ended;
    static uint8_t prev, hidden;
    static int log = -1;
    uint64_t now = ff_now_ms();
    uint8_t press = (uint8_t)(buttons & ~prev & 0x30);
    if (!s_pad_t0) s_pad_t0 = now;
    {
        static uint32_t last_slot = 0x12345678u;
        uint32_t sl = MEM32(0x0051DF64u);
        if (sl != last_slot) {
            fprintf(stderr, "[save] slot %d (camera mode %u)\n", (int)sl, MEM32(CAMERA_MODE));
            last_slot = sl;
        }
    }
    dev_warp();
    dev_save_now();
    {   /* Dev: HPCOS_PAUSE_OPTS=<mask> ORs the pause menu's debug rows into [0x527F26]
         * (0x200 the Level Selector, HP-COS) */
        static int m = -1;
        if (m < 0) { const char *e = getenv("HPCOS_PAUSE_OPTS"); m = e ? (int)strtol(e, NULL, 0) : 0; }
        if (m) MEM16(0x00527F26u) |= (uint16_t)m;
    }
    int cut = MEM32(CAMERA_MODE) == 7u;
    prev = buttons;
    if (log < 0) { const char *e = getenv("HPCOS_FF_LOG"); log = e && *e == '1'; }
    if (log) {                          /* game input reads a second (~ the title's frames) */
        static uint64_t t0; static unsigned n;
        n++;
        if (now - t0 >= 1000) {
            if (t0) fprintf(stderr, "[skip] %u pad reads/s, camera mode %u, ff %d\n", n, MEM32(CAMERA_MODE), s_ff);
            t0 = now; n = 0;
        }
    }
    if (log && press)
        fprintf(stderr, "[skip] %s pressed, camera mode %u\n", press & 0x10 ? "Start" : "Back", MEM32(CAMERA_MODE));
    hidden &= buttons;                  /* a hidden press stays hidden until released */
    if (s_ff) {
        /* stop at the first pass out of the cutscene: at ~30x even a second of grace would be
         * half a minute of the game played fast */
        if (!cut || now - ff_since > 180000) {
            ended = !cut ? now : 0;
            ff_set(0);
        }
    } else if (cut && ended && now - ended < 2500 && ff_enabled()) {
        ended = 0;                      /* the next part of a cutscene in two goes on fast too */
        ff_since = now;
        ff_set(1);
    } else if (cut && press && ff_enabled()) {
        ff_since = now;
        hidden |= press;
        ff_set(1);
    }
    return (uint8_t)(buttons & ~hidden);
}

/* While a cutscene is skipped the title's frame loop (0x13170: logic, then the world's
 * render sub_00035190, then D3DDevice_Swap sub_00134D70) renders and swaps only about
 * every 250 ms. The render waits on the GPU for push-buffer and vertex-buffer space and
 * Swap on the previous frame and the vblank: ~7 ms of GPU handshakes a pass, which kept
 * the fast-forward at ~2.4x. Left out, a pass is the logic alone (one fixed step), so
 * the cutscene runs as fast as the CPU allows. The render's own bookkeeping stays:
 * its prep (sub_00038C80) and its counters; only the scene's draw (a virtual call) is
 * left out. HPCOS_FF_RENDER_MS=0: every pass renders and swaps. */
static int s_ff_swap;                       /* this pass rendered: its Swap is made */
static void ff_call(void (*fn)(void), uint32_t ret_va)
{
    esp -= 4;
    MEM32(esp) = ret_va;                    /* as the title's call: a return address */
    fn();                                   /* cdecl, no arguments: pops the return address */
}
extern void sub_00035190_gen(void);
extern void sub_00038C80(void);
void sub_00035190(void)
{
    static int ms = -1;
    static uint64_t last;
    if (ms < 0) { const char *e = getenv("HPCOS_FF_RENDER_MS"); ms = e ? atoi(e) : 250; }
    if (s_ff && ms > 0) {
        uint64_t now = ff_now_ms();
        if (now - last < (uint64_t)ms) {
            MEM32(0x0051DEA4u) = 2;
            ff_call(sub_00038C80, 0x000351ABu);
            MEM32(0x0051DEA8u) += 1;
            MEM32(0x0050C350u) = 0;
            s_ff_swap = 0;
            esp += 4;                       /* ret */
            return;
        }
        last = now;
    }
    s_ff_swap = 1;
    sub_00035190_gen();
}

/* D3DDevice_Swap (sub_00134D70, stdcall(flags), returns the swap count): made only
 * after a pass that rendered (above) while skipping. */
extern void sub_00134D70_gen(void);
void sub_00134D70(void)
{
    if (s_ff && !s_ff_swap) {
        eax = MEM32(MEM32(0x00140888u) + 0x2010);    /* the device's swap count, as Swap returns */
        esp += 4 + 4;                                /* ret 4 */
        return;
    }
    sub_00134D70_gen();
}

/* XInputGetState(hDevice, pState): dwPacketNumber, then XINPUT_GAMEPAD
 * { wButtons, bAnalogButtons[8], sThumbLX, LY, RX, RY } -- the USB report's
 * bytes 2..19 laid out the same way. The packet number moves only on a
 * change, as XAPI's does. HPCOS_PAD_LOG=1 logs every change. */
extern void sub_0017743B_gen(void);
void sub_0017743B(void)
{
    uint32_t st = ARG(1);
    uint8_t rep[20];
    int i = xi_hle() ? xi_index(ARG(0)) : -2, k;
    if (i == -2) { sub_0017743B_gen(); return; }
    int have = i >= 0 && usb_gamepad_connected((int)s_xi[i].port)
            && usb_gamepad_report((int)s_xi[i].port, rep, (int)sizeof rep) >= 20;
    if (have)
        rep[2] = cutscene_ff_pad(rep[2]);
    if (i < 0) {
        eax = 6;
    } else if (!have) {
        eax = 1167;
    } else {
        static int log = -1;
        {
            if (memcmp(rep + 2, s_xi[i].last, 16)) {
                s_xi[i].packet++;
                if (log < 0) { const char *e = getenv("HPCOS_PAD_LOG"); log = e && *e == '1'; }
                if (log)
                    fprintf(stderr, "[pad] %llu ms port %u buttons %02X analog %02X %02X %02X %02X %02X %02X %02X %02X"
                            " LX %d LY %d RX %d RY %d\n", (unsigned long long)ff_now_ms(), s_xi[i].port, rep[2], rep[4], rep[5], rep[6], rep[7],
                            rep[8], rep[9], rep[10], rep[11], (int16_t)(rep[12] | rep[13] << 8),
                            (int16_t)(rep[14] | rep[15] << 8), (int16_t)(rep[16] | rep[17] << 8),
                            (int16_t)(rep[18] | rep[19] << 8));
            }
            {   /* HPCOS_CAPTURE_ON_BACK=<n>: Back pressed logs the next n frames' draws (as F12) */
                static int cob = -1;
                extern volatile int nv2a_mtl_capture, nv2a_mtl_capture_frames;
                if (cob < 0) { const char *e = getenv("HPCOS_CAPTURE_ON_BACK"); cob = e ? atoi(e) : 0; }
                if (cob > 0 && (rep[2] & 0x20) && !(s_xi[i].last[0] & 0x20)) {
                    nv2a_mtl_capture_frames = cob;
                    nv2a_mtl_capture = cob + 1;
                    fprintf(stderr, "[capture] Back: %d frames\n", cob);
                }
            }
            memcpy(s_xi[i].last, rep + 2, 16);
        }
        MEM32(st) = s_xi[i].packet;
        MEM16(st + 4) = rep[2];
        for (k = 0; k < 8; k++) MEM8(st + 6 + k) = rep[4 + k];
        for (k = 0; k < 8; k++) MEM8(st + 14 + k) = rep[12 + k];
        eax = 0;
    }
    esp += 4 + 8;
}

/* XInputSetState(hDevice, pFeedback): the rumble goes to the host pad and the
 * request completes at once. XAPI would complete it from the pad's interrupt
 * OUT pipe, which never answers here: HP-PS found HPCOS waiting on its event
 * with INFINITE the first time Harry was hit. XINPUT_FEEDBACK: header
 * { dwStatus, hEvent, ... } at 0, XINPUT_RUMBLE { left, right } at 0x42. */
void sub_001774A7(void)
{
    uint32_t fb = ARG(1);
    int i = xi_index(ARG(0));
    if (fb) {
        uint8_t out[6] = { 0x00, 0x06, MEM8(fb + 0x42), MEM8(fb + 0x43), MEM8(fb + 0x44), MEM8(fb + 0x45) };
        uint32_t event = MEM32(fb + 4);
        usb_gamepad_output(i >= 0 ? (int)s_xi[i].port : 0, out, 6);
        MEM32(fb) = 0;                              /* ERROR_SUCCESS */
        if (event)
            xbox_GuestSetEvent(event);
    }
    eax = 997;                                      /* ERROR_IO_PENDING, as XAPI returns */
    esp += 4 + 8;
}
#undef ARG

/* ── Debug: HPCOS_PEEK="addr:f,addr:x,..." -- guest words once a second ──
 * f = float, x = hex dword, d = int; printed from the vblank DPC hook (the
 * runtime's weak xbox_after_vblank_dpc). */
/* HPCOS_MEMSNAP=<dir>: .data (0x210F20-0x375330) to <dir>/s<vblank>.bin every 2 s
 * (find state flags by diffing snapshots taken in different game states). */
static void memsnap(void)
{
    static int on = -1; static char dir[512]; static unsigned t;
    if (on < 0) { const char *e = getenv("HPCOS_MEMSNAP"); on = e && *e; if (on) snprintf(dir, sizeof dir, "%s", e); }
    if (!on || ++t % 120)
        return;
    {
        char fn[600]; FILE *f;
        snprintf(fn, sizeof fn, "%s/s%07u.bin", dir, t);
        if ((f = fopen(fn, "wb"))) { fwrite((const void *)XBOX_PTR(0x18E680u), 1, 0x54EDB0u - 0x18E680u, f); fclose(f); }
    }
}

void xbox_after_vblank_dpc(void)
{
    static int on = -1, n;
    memsnap();
    static uint32_t addr[16];
    static char kind[16];
    static unsigned ticks;
    if (on < 0) {
        const char *e = getenv("HPCOS_PEEK");
        on = 0;
        while (e && *e && n < 16) {
            char *end;
            addr[n] = (uint32_t)strtoul(e, &end, 16);
            kind[n] = (*end == ':' && end[1]) ? end[1] : 'x';
            n++;
            e = strchr(end, ',');
            if (e) e++;
        }
        on = n > 0;
    }
    if (!on || ++ticks % 60)
        return;
    {
        int k;
        fprintf(stderr, "[peek]");
        for (k = 0; k < n; k++) {
            if (kind[k] == 'f') fprintf(stderr, " %06X=%g", addr[k], MEMF(addr[k]));
            else if (kind[k] == 'd') fprintf(stderr, " %06X=%d", addr[k], (int32_t)MEM32(addr[k]));
            else fprintf(stderr, " %06X=%08X", addr[k], MEM32(addr[k]));
        }
        fprintf(stderr, "\n");
    }
}

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}


/* ── Movies: the NV2A video overlay on the host ───────────────────────────
 * From MiddleDucky/HP-COS-Xbox-Recomp (MIT), port/src/hle_video.c, same NTSC
 * addresses. The XMV player decodes each frame into a YUY2 surface and hands it
 * to D3DDevice_UpdateOverlay, which points PVIDEO at it; nothing scans PVIDEO
 * out here, and EnableOverlay(FALSE) spun forever waiting for the overlay
 * engine to stop. The frame goes to the Metal backend's movie layer instead
 * (nv2a_metal.m). HPCOS_HLE_OVERLAY=0 runs the XDK's own functions. */
extern void nv2a_mtl_movie_frame(const uint32_t *xrgb, uint32_t w, uint32_t h, int dx, int dy, int dw, int dh);
extern void nv2a_mtl_movie_off(void);
extern uint32_t xbox_Nv2aVblankCount(void);
#define OV_ARG(i) MEM32(esp + 4u + 4u * (i))

static int overlay_hle(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("HPCOS_HLE_OVERLAY");
        on = !(e && *e == '0');
        fprintf(stderr, "[movie] overlay: %s\n", on ? "host movie layer" : "XDK (PVIDEO, not shown)");
    }
    return on;
}

static uint8_t ov_clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
static uint32_t ov_yuv(int y, int u, int v)   /* BT.601 studio range */
{
    int c = 298 * (y - 16), d = u - 128, e = v - 128;
    return (uint32_t)ov_clamp8((c + 409 * e + 128) >> 8) << 16 |
           (uint32_t)ov_clamp8((c - 100 * d - 208 * e + 128) >> 8) << 8 |
           (uint32_t)ov_clamp8((c + 516 * d + 128) >> 8);
}

/* D3DDevice_UpdateOverlay(pSurface, SrcRect, DstRect, EnableColorKey, ColorKey), stdcall.
 * D3DSurface: Common, Data (physical), Lock, Format (bits 8-15), Size (w-1 | h-1 << 12 | pitch/64-1 << 24). */
extern void sub_00136520_gen(void);
void sub_00136520(void)
{
    static uint32_t *frame, cap;
    static int logged;
    uint32_t surf = OV_ARG(0), src = OV_ARG(1), dst = OV_ARG(2), fmt, size, w, h, pitch, base;
    int sx = 0, sy = 0, sw, sh, dx = 0, dy = 0, dw = 640, dh = 480;

    if (!overlay_hle()) { sub_00136520_gen(); return; }
    if (!surf) { nv2a_mtl_movie_off(); eax = 0; esp += 4 + 20; return; }
    fmt = (MEM32(surf + 0xC) >> 8) & 0xFF;
    size = MEM32(surf + 0x10);
    w = (size & 0xFFF) + 1; h = ((size >> 12) & 0xFFF) + 1; pitch = (((size >> 24) & 0xFF) + 1) * 64;
    base = MEM32(surf + 4);
    if (base < 0x04000000u) base += 0x80000000u;
    sw = (int)w; sh = (int)h;
    if (src) { sx = (int)MEM32(src); sy = (int)MEM32(src + 4); sw = (int)MEM32(src + 8) - sx; sh = (int)MEM32(src + 12) - sy; }
    if (dst) { dx = (int)MEM32(dst); dy = (int)MEM32(dst + 4); dw = (int)MEM32(dst + 8) - dx; dh = (int)MEM32(dst + 12) - dy; }
    if (logged++ < 2)
        fprintf(stderr, "[movie] overlay surface %08X fmt %02X %ux%u pitch %u src %d,%d %dx%d dst %d,%d %dx%d\n",
                surf, fmt, w, h, pitch, sx, sy, sw, sh, dx, dy, dw, dh);
    if ((fmt != 0x24 && fmt != 0x25) || sw <= 0 || sh <= 0 || sx < 0 || sy < 0
            || (uint32_t)(sx + sw) > w || (uint32_t)(sy + sh) > h) {
        eax = 0; esp += 4 + 20; return;
    }
    if ((uint32_t)(sw * sh) > cap) {
        free(frame);
        frame = (uint32_t *)malloc((size_t)sw * sh * 4);
        cap = frame ? (uint32_t)(sw * sh) : 0;
        if (!frame) { eax = 0; esp += 4 + 20; return; }
    }
    for (int y = 0; y < sh; y++) {
        const uint8_t *row = (const uint8_t *)XBOX_PTR(base + (uint32_t)(sy + y) * pitch);
        uint32_t *out = frame + (size_t)y * sw;
        for (int x = 0; x < sw; x++) {
            int px = sx + x;
            const uint8_t *m = row + (px & ~1) * 2;   /* one macropixel: two pixels */
            out[x] = fmt == 0x24 ? ov_yuv(m[(px & 1) * 2], m[1], m[3])
                                 : ov_yuv(m[1 + (px & 1) * 2], m[0], m[2]);
        }
    }
    nv2a_mtl_movie_frame(frame, (uint32_t)sw, (uint32_t)sh, dx, dy, dw, dh);
    eax = 0;
    esp += 4 + 20;
}

/* D3DDevice_EnableOverlay(Enable), stdcall: show or hide the movie layer. */
extern void sub_001366A0_gen(void);
void sub_001366A0(void)
{
    if (!overlay_hle()) { sub_001366A0_gen(); return; }
    fprintf(stderr, "[movie] EnableOverlay(%u)\n", OV_ARG(0));
    if (!OV_ARG(0)) nv2a_mtl_movie_off();
    eax = 0;
    esp += 4 + 4;
}

/* D3DDevice_GetDisplayFieldStatus(pFieldStatus), stdcall. The XMV decoder polls it
 * first thing (the call returns to 0x14F063) and the player loop goes round at once
 * when no frame is due: millions of calls a second. There, wait for the next field
 * (HP-COS: spinning starved the movie's own clock). */
extern void sub_00133790_gen(void);
void sub_00133790(void)
{
    if (MEM32(esp) == 0x0014F063u) {
        static uint32_t last = 0xFFFFFFFFu;
        uint32_t v = xbox_Nv2aVblankCount();
        for (int ms = 0; v == last && ms < 20; ms++) { usleep(1000); v = xbox_Nv2aVblankCount(); }
        last = v;
    }
    sub_00133790_gen();
}
#undef OV_ARG

/* ── File cache: the console's DVD latency between "queue a read" and "register its callback" ──
 * The file cache (worker thread 0x1146C0, loop 0x1125A0) serves read requests that a game
 * thread queues with sub_001133F0 and then hooks with sub_00112E20(request, callback). The
 * hook finds the request queued, in progress, or finished-but-not-reaped and attaches or
 * calls the callback; a request the worker has already reaped gets none, and the reader that
 * chains 8 KB chunks through that callback (0x1136C0) never finishes: the main thread polls
 * it forever (a hang at a loading screen, ~1 boot in 8). On the console the worker blocks on
 * the DVD for milliseconds, so the hook always came first; here reads complete at once. So
 * while the main thread is between the two calls, the worker waits at the top of its loop
 * (before it takes the queue's lock), 200 ms at most. The main thread also queues reads it
 * only polls: its next file-cache call (another request, a status poll) ends the hold, as a
 * hold kept to the bound starved the menu music (0.25 s of sound a second). The worker's own chained requests
 * (from 0x1136C0, return 0x11371E) do not count. HPCOS_CACHE_HOLD=0 turns it off. */
static volatile int s_fc_owner;            /* thread number of the game thread between the calls, 0 none */
static volatile uint64_t s_fc_since;
static _Thread_local int t_fc_me;
static volatile int s_fc_next;

static int fc_me(void)
{
    if (!t_fc_me) t_fc_me = __atomic_add_fetch(&s_fc_next, 1, __ATOMIC_RELAXED);
    return t_fc_me;
}

static uint64_t fc_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int fc_trace(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("HPCOS_FC_TRACE"); on = e && *e == '1'; }
    return on;
}

static int fc_hold_on(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("HPCOS_CACHE_HOLD"); on = !(e && *e == '0'); }
    return on;
}

extern void sub_001133F0_gen(void);
void sub_001133F0(void)
{
    int me = fc_me();
    {
        uint32_t ret = MEM32(esp), a0 = MEM32(esp + 4), a1 = MEM32(esp + 8);
        sub_001133F0_gen();
        if (fc_trace()) fprintf(stderr, "[fc] t%d queue(%08X,%08X) from %08X -> %08X\n", me, a0, a1, ret, eax);
    }
}

/* Every queue function (0x1133F0 read, 0x113300, 0x113460, ...) allocates its request here
 * (under the request pool's lock) before it queues it and sets the worker's event. */
static _Thread_local int t_fc_worker;
extern void sub_00113090_gen(void);
void sub_00113090(void)
{
    extern int xbox_is_guest_main(void);
    /* the main thread only: it hooks every request it queues. The music streamer
     * (0x117A97) queues and polls without a hook; holding the worker for it
     * starved the menu music (0.25 s of sound a second). */
    if (fc_hold_on() && !t_fc_worker && xbox_is_guest_main()) {
        s_fc_since = fc_now_ns();
        __atomic_store_n(&s_fc_owner, fc_me(), __ATOMIC_RELEASE);
    }
    if (fc_trace()) fprintf(stderr, "[fc] t%d alloc from %08X main %d\n", fc_me(), MEM32(esp), xbox_is_guest_main());
    sub_00113090_gen();
}

extern void sub_00112E20_gen(void);
void sub_00112E20(void)
{
    int me = fc_me();
    uint32_t ret = MEM32(esp), h = MEM32(esp + 4), cb = MEM32(esp + 8);
    sub_00112E20_gen();
    if (fc_trace()) fprintf(stderr, "[fc] t%d hook(%08X,%08X) from %08X -> %08X\n", me, h, cb, ret, eax);
    if (s_fc_owner == me) __atomic_store_n(&s_fc_owner, 0, __ATOMIC_RELEASE);
}

extern void sub_00114EB0_gen(void);
void sub_00114EB0(void)
{
    if (MEM32(esp) == 0x001125DEu && fc_hold_on()) {     /* the worker's loop, before its queue lock */
        int me = fc_me(), owner;
        t_fc_worker = 1;
        while ((owner = __atomic_load_n(&s_fc_owner, __ATOMIC_ACQUIRE)) && owner != me
               && fc_now_ns() - s_fc_since < 200000000ull) {
            int d = xbox_gil_suspend();
            usleep(100);
            xbox_gil_resume(d);
        }
    }
    sub_00114EB0_gen();
}

/* The worker's wait for work (sub_00114A10, called at 0x112636 when its queue is empty):
 * WaitForSingleObject(event, INFINITE) becomes 50 ms waits. A wake-up lost between the
 * queue's SetEvent and this wait left the main thread polling a request nobody served (a
 * stall at boot or a loading screen); a timeout only sends the worker round its loop, which
 * finds the queue empty and waits again. HPCOS_FC_WAIT_MS=0: INFINITE as the title. */
extern void sub_0010E5D2(void);
void sub_00114A10(void)
{
    static int ms = -1;
    uint32_t ret = MEM32(esp), h = MEM32(MEM32(esp + 4) + 4);
    if (ms < 0) { const char *e = getenv("HPCOS_FC_WAIT_MS"); ms = e ? atoi(e) : 50; }
    esp -= 4; MEM32(esp) = (ret == 0x0011263Bu && ms > 0) ? (uint32_t)ms : 0xFFFFFFFFu;
    esp -= 4; MEM32(esp) = h;
    esp -= 4; MEM32(esp) = 0x00114A1Fu;     /* as the title's call: its return address */
    sub_0010E5D2();                         /* WaitForSingleObject, stdcall: pops it and the 2 args */
    esp += 4;                               /* our own return */
}

/* HPCOS_FC_TRACE=1: the file cache's list operations -- take the head (sub_00114BF0(list)
 * -> item) and push (sub_00114B60(list, item)); the item's id [+4] and kind [+8]. */
extern void sub_00114BF0_gen(void);
void sub_00114BF0(void)
{
    uint32_t list = MEM32(esp + 4), ret = MEM32(esp);
    sub_00114BF0_gen();
    if (fc_trace() && eax)
        fprintf(stderr, "[fc] t%d take %08X from list %08X (ret %08X): id %08X kind %u\n", fc_me(), eax, list, ret,
                MEM32(eax + 4), MEM32(eax + 8));
}
extern void sub_00114B60_gen(void);
void sub_00114B60(void)
{
    uint32_t list = MEM32(esp + 4), item = MEM32(esp + 8), ret = MEM32(esp);
    if (fc_trace() && item)
        fprintf(stderr, "[fc] t%d push %08X to list %08X (ret %08X): id %08X kind %u cb %08X status %d err %08X\n", fc_me(),
                item, list, ret, MEM32(item + 4), MEM32(item + 8), MEM32(item + 0x20), (int8_t)MEM8(item + 0x10),
                MEM32(item + 0x14));
    sub_00114B60_gen();
}

/* The boot loader's file-cache callbacks (cb(id, status, value), cdecl; called on the
 * cache's worker thread) published the loader's next state [0x1BE8C8] FIRST, then
 * released the request (sub_00112BA0) and stored its result (the file id at 0x436F40 /
 * 0x436F44). The release blocks on the cache's lock while the main thread holds it to
 * poll, so the main thread saw the new state, read the previous file's id and seeked a
 * closed file: STATUS_INVALID_HANDLE, a failed request, and the loader waited for good
 * (a stall at boot, ~1 in 15-30). The console's scheduler made that window vanishingly
 * small. Same work, state last. HPCOS_LOADER_ORDER=0: the title's order. */
static int loader_order_on(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("HPCOS_LOADER_ORDER"); on = !(e && *e == '0'); }
    return on;
}
static uint32_t fc_release(uint32_t id)
{
    esp -= 4; MEM32(esp) = id;
    esp -= 4; MEM32(esp) = 0;               /* return address */
    sub_00112BA0();                         /* cdecl: pops the return address */
    esp += 4;                               /* the argument */
    return eax;
}
static void loader_cb(uint32_t state_va, uint32_t state, uint32_t store_va, uint32_t clear_va)
{
    if (MEM32(esp + 8) == 1u) {
        uint32_t r = fc_release(MEM32(esp + 4));
        if (store_va) MEM32(store_va) = r;
        if (clear_va) MEM32(clear_va) = 0;
        MEM32(state_va) = state;
    }
    esp += 4;                               /* ret */
}
/* every callback of this shape in the title (a scan for cmp [esp+8],1 / mov [state],n /
 * call sub_00112BA0): the boot loader [0x1BE8C8] and the level loader [0x436F2C]. Written out
 * one by one: the lifter finds overrides by their definitions. */
extern void sub_000163E0_gen(void);
void sub_000163E0(void) { if (loader_order_on()) loader_cb(0x001BE8C8u, 4, 0, 0x00436F60u); else sub_000163E0_gen(); }
extern void sub_00016410_gen(void);
void sub_00016410(void) { if (loader_order_on()) loader_cb(0x001BE8C8u, 1, 0x00436F40u, 0); else sub_00016410_gen(); }
extern void sub_00016440_gen(void);
void sub_00016440(void) { if (loader_order_on()) loader_cb(0x001BE8C8u, 5, 0, 0); else sub_00016440_gen(); }
extern void sub_00016460_gen(void);
void sub_00016460(void) { if (loader_order_on()) loader_cb(0x001BE8C8u, 9, 0, 0x00436F60u); else sub_00016460_gen(); }
extern void sub_00016490_gen(void);
void sub_00016490(void) { if (loader_order_on()) loader_cb(0x001BE8C8u, 7, 0x00436F44u, 0); else sub_00016490_gen(); }
extern void sub_000164C0_gen(void);
void sub_000164C0(void) { if (loader_order_on()) loader_cb(0x001BE8C8u, 8, 0, 0); else sub_000164C0_gen(); }
extern void sub_000164E0_gen(void);
void sub_000164E0(void) { if (loader_order_on()) loader_cb(0x00436F2Cu, 12, 0x00436F58u, 0); else sub_000164E0_gen(); }
extern void sub_00016510_gen(void);
void sub_00016510(void) { if (loader_order_on()) loader_cb(0x00436F2Cu, 15, 0, 0); else sub_00016510_gen(); }
extern void sub_00016530_gen(void);
void sub_00016530(void) { if (loader_order_on()) loader_cb(0x00436F2Cu, 20, 0, 0); else sub_00016530_gen(); }
extern void sub_00016550_gen(void);
void sub_00016550(void) { if (loader_order_on()) loader_cb(0x00436F2Cu, 19, 0, 0); else sub_00016550_gen(); }
extern void sub_00016570_gen(void);
void sub_00016570(void) { if (loader_order_on()) loader_cb(0x00436F2Cu, 17, 0x00436F5Cu, 0); else sub_00016570_gen(); }
extern void sub_000165A0_gen(void);
void sub_000165A0(void) { if (loader_order_on()) loader_cb(0x00436F2Cu, 18, 0, 0); else sub_000165A0_gen(); }

/* HPCOS_SAVE_LOG=1: the save checks -- free space for a save (sub_00016D40 -> 1 enough),
 * blocks short (sub_00018AD0(blocks needed) -> needed - free), saves whose name is not POTTER A/B/C (sub_00018570: foreign saves, normally 0). */
static int save_log(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("HPCOS_SAVE_LOG"); on = e && *e == '1'; }
    return on;
}
extern void sub_00016D40_gen(void);
void sub_00016D40(void)
{
    uint32_t ret = MEM32(esp);
    sub_00016D40_gen();
    if (save_log()) fprintf(stderr, "[save] enough space for a save? %d (from %08X)\n", (int)eax, ret);
}
extern void sub_00018AD0_gen(void);
void sub_00018AD0(void)
{
    uint32_t ret = MEM32(esp), need = MEM32(esp + 4);
    sub_00018AD0_gen();
    if (save_log()) fprintf(stderr, "[save] blocks: need %u, short by %d (from %08X)\n", need, (int)eax, ret);
}
extern void sub_00018570_gen(void);
void sub_00018570(void)
{
    uint32_t ret = MEM32(esp);
    sub_00018570_gen();
    if (save_log()) fprintf(stderr, "[save] saves with other names: %d (from %08X)\n", (int)eax, ret);
}

/* The save slot [0x51DF64] (picked at New Game; -1: "started without the save option").
 * A save scroll does nothing while it is -1 (0x80EB0). The game systems' init
 * (sub_0004ABF0, from the game mode's init 0x34D00) sets it to -1 with the rest. In
 * play it is kept: the owner's game, after the Burrow's End Day, had saving off and
 * its scrolls did nothing. HPCOS_KEEP_SLOT=0: the title's way. Every change of the
 * slot is logged ([save] slot). */
extern void sub_0004ABF0_gen(void);
void sub_0004ABF0(void)
{
    static int keep = -1;
    uint32_t slot = MEM32(0x0051DF64u), ent = MEM32(0x00530408u);
    if (keep < 0) { const char *e = getenv("HPCOS_KEEP_SLOT"); keep = !(e && *e == '0'); }
    sub_0004ABF0_gen();
    fprintf(stderr, "[save] game systems init: slot %d -> %d (in play: %d)\n", (int)slot,
            (int)MEM32(0x0051DF64u), ent && MEM32(ent + 0xAC) ? 1 : 0);
    if (keep && slot != 0xFFFFFFFFu && ent && MEM32(ent + 0xAC)) {
        MEM32(0x0051DF64u) = slot;
        fprintf(stderr, "[save] slot %d kept\n", (int)slot);
    }
}

static void fc_owner_moves_on(void)
{
    static int rel = -1;
    if (rel < 0) { const char *e = getenv("HPCOS_CACHE_RELEASE"); rel = !(e && *e == '0'); }
    if (rel && s_fc_owner == fc_me()) __atomic_store_n(&s_fc_owner, 0, __ATOMIC_RELEASE);
}

/* the other status polls end the hold too */
extern void sub_001129D0_gen(void);
void sub_001129D0(void) { fc_owner_moves_on(); sub_001129D0_gen(); }
extern void sub_00112BA0_gen(void);
void sub_00112BA0(void) { fc_owner_moves_on(); sub_00112BA0_gen(); }

/* HPCOS_FC_TRACE=1: the file cache's status poll sub_001138B0(handle): its entry once a second while polled */
extern void sub_001138B0_gen(void);
void sub_001138B0(void)
{
    uint32_t h = MEM32(esp + 4);
    fc_owner_moves_on();
    sub_001138B0_gen();
    if (fc_trace()) {
        static uint64_t last; static uint32_t lh;
        uint64_t now = fc_now_ns();
        if (h != lh || now - last > 1000000000ull) {
            uint32_t tab = MEM32(0x0054AE2Cu), e = tab + (h & 0xFF) * 48u;
            last = now; lh = h;
            fprintf(stderr, "[fc] poll %08X -> %08X entry %08X: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
                    h, eax, e, MEM32(e), MEM32(e+4), MEM32(e+8), MEM32(e+12), MEM32(e+16), MEM32(e+20), MEM32(e+24),
                    MEM32(e+28), MEM32(e+32), MEM32(e+36), MEM32(e+40), MEM32(e+44));
        }
    }
}

/* ── Skip Logo Movies (overlay: Tweaks, HPCOS_SKIP_LOGOS=1) ─────────────────
 * From HP-COS-Xbox-Recomp (MIT, tools/patch_gen.py + app.cpp hpcos_SkipMovie):
 * the movie player sub_00018D40(name, skippable) is cdecl and no caller reads
 * its result, so returning at once is the movie having ended. The boot plays
 * warner.xmv and the language's ea_logo_*.xmv; the story intro stays. */
extern void sub_00018D40_gen(void);
void sub_00018D40(void)
{
    uint32_t name = MEM32(esp + 4u);
    const char *n = name ? (const char *)XBOX_PTR(name) : NULL, *b;
    const char *e = getenv("HPCOS_SKIP_LOGOS");
    if (n) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        fprintf(stderr, "[movie] %.3f s %s\n", ts.tv_sec + ts.tv_nsec / 1e9, n);
        b = strrchr(n, '\\');
        b = b ? b + 1 : n;
        if (e && *e == '1' && (!strcasecmp(b, "warner.xmv") || !strncasecmp(b, "ea_logo_", 8)
                               || !strcasecmp(b, "ealogo.xmv"))) {
            fprintf(stderr, "[movie] skipped %s (Skip Logo Movies)\n", n);
            eax = 0;
            esp += 4;                           /* cdecl: only the return address */
            return;
        }
    }
    sub_00018D40_gen();
}

/* Debug: HPCOS_DS_TRACE=1 -- IDirectSoundBuffer::GetCurrentPosition(this, &play, &write), stdcall,
 * each buffer's cursors and the caller, at most 400 lines (the music streamer's view). */
extern void sub_00145337_gen(void);
void sub_00145337(void)
{
    static int on = -1, n;
    uint32_t self = MEM32(esp + 4), pp = MEM32(esp + 8), pw = MEM32(esp + 12), ret = MEM32(esp);
    if (on < 0) { const char *e = getenv("HPCOS_DS_TRACE"); on = e && *e == '1'; }
    sub_00145337_gen();
    if (on && n++ < 400) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        fprintf(stderr, "[ds] %.3f buf %08X play %u write %u from %08X -> %08X\n", ts.tv_sec + ts.tv_nsec / 1e9,
                self, pp ? MEM32(pp) : 0xFFFFFFFFu, pw ? MEM32(pw) : 0xFFFFFFFFu, ret, eax);
    }
}
