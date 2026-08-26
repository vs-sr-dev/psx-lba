/*
 * psx_m6.c — M6: the autopilot, and what the hero did about it.
 *
 * M5 left a game loop that nobody could command. The pad mapping that answers
 * that is in psx_sys.c; this file is how it gets proved.
 *
 * The problem is that a pad cannot be pressed from a log file. Every number in
 * this port so far came out of `grep TTY duckstation.log`, and an input path
 * verified by a human holding a controller and saying "yes, he walked" is the
 * one measurement in the project that could not be repeated, diffed, or
 * checked against a previous build. So the pad gets a stand-in: a table of
 * (direction, buttons, duration) that PORT_ScanInput consults instead of the
 * hardware, feeding the engine's Joy/Fire/Key exactly as a player would, at
 * the same 50 Hz, through the same globals.
 *
 * What that buys is a falsifiable claim per step. "forward, 150 ticks" is
 * followed in the log by the hero's position before and after, and 3 seconds
 * of held UP either moved him or it did not. Rotation is the same question
 * asked of Beta, and the behaviour panel is the same question asked of
 * Comportement.
 *
 * The script runs once and then hands the pad back, so the same build is both
 * the test and something to play: the autopilot walks Twinsen around for
 * twenty-five seconds and then gets out of the way.
 *
 * The two modal screens at the end are the interesting part, because a modal
 * is where a pad mapping stops being a table and starts being a timing
 * problem. The behaviour panel is held open by L1 and closes when it is
 * released, which a script can do exactly. The inventory closes on a SECOND
 * press of the same button that opened it, so the release in between has to
 * be long enough for the engine's de-repeat to clear and the second press
 * short enough to be over before MainLoop looks again -- otherwise it
 * reopens. Both windows are in the table, with the reasoning next to them.
 *
 * Unlike the rest of platform/psx this file includes the engine headers, for
 * the same reason psx_m4.c does: it has to name ListObjet and Comportement,
 * and a local re-declaration of a struct the engine owns is how a port ends
 * up with two disagreeing ideas of a layout.
 */

/* The whole file is the harness. Without the knob it is not in the image. */
#ifdef PORT_PSX_M6_AUTOPILOT

#include "watcom_compat.h"
#include "c_extern.h"

/* port.h is not included: ADELINE.H defines ULONG and friends as macros and
 * port.h typedefs the same names. */
void PORT_Diag(const char *fmt, ...);

/* ══════════════════════════════════════════════════════════════════════════
 * The script
 *
 * Durations are 50 Hz ticks, because PORT_ScanInput is called from the tick
 * ISR and not from the frame loop. That is the right clock for this: the
 * frame is 34 ms and the tick is 20 ms, so the game already misses ticks, and
 * a script counted in frames would run at whatever speed the renderer
 * happened to manage. A player's thumb does not slow down when the scene gets
 * busy, and neither does this.
 *
 * The releases between steps are not padding. GAMEMENU.C de-repeats input
 * with the `flag` idiom — sample once, then blank everything until nothing is
 * held — so a step that goes straight from one direction to another is one
 * continuous press as far as any menu is concerned.
 * ═════════════════════════════════════════════════════════════════════════ */

typedef struct
{
	const char *name;
	unsigned short ticks;
	unsigned short joy;
	unsigned short fire;
	unsigned short key;
} M6_STEP;

static const M6_STEP script[] = {
	/* Let the scene finish arriving. The first frame carries ChangeCube. */
	{"settle",           100, 0,        0,       0},

	/* The four directions, each long enough that a single dropped frame
	 * cannot be mistaken for the whole thing not working.
	 *
	 * Backward comes second, before either turn, and that ordering is the
	 * difference between a measurement and a coincidence: it retraces ground
	 * Twinsen has just walked, which is known to be clear. The first version
	 * of this table reversed him into a wall he had walked up to, reported
	 * no movement, and looked exactly like a dead J_DOWN. */
	{"forward",          150, J_UP,     0,       0},
	{"release",           25, 0,        0,       0},
	{"backward",         100, J_DOWN,   0,       0},
	{"release",           25, 0,        0,       0},
	{"turn left",         60, J_LEFT,   0,       0},
	{"release",           25, 0,        0,       0},
	{"forward again",    150, J_UP,     0,       0},
	{"release",           25, 0,        0,       0},
	{"turn right",       120, J_RIGHT,  0,       0},
	{"release",           25, 0,        0,       0},

	/* The verbs. Cross is the action; Square throws, and at cube 0 Twinsen
	 * has no magic ball, so the interesting part is only that the bit
	 * arrives and nothing falls over. */
	{"action (X)",        25, 0,        F_SPACE, 0},
	{"release",           50, 0,        0,       0},
	{"throw (square)",    25, 0,        F_ALT,   0},
	{"release",           50, 0,        0,       0},

	/* The behaviour panel: held open by L1, direction picks, release
	 * commits. Comportement in the log is the whole assertion.
	 *
	 * Three seconds on the open, and that number is measured rather than
	 * generous. MenuComportement draws four animated bodies before it looks
	 * at the pad for the first time, and it reads the LIVE Fire when it does,
	 * not the MyFire that MainLoop sampled to get here -- so a hold shorter
	 * than the drawing is a panel that opens, closes and changes nothing. At
	 * 1.4 s that is exactly what happened. */
	{"behaviour open",   150, 0,        F_CTRL,  0},
	{"behaviour next",    60, J_RIGHT,  F_CTRL,  0},
	{"behaviour commit",  60, 0,        0,       0},

	{"settle",           100, 0,        0,       0},
	/* The inventory, opened and closed. The close is short on purpose:
	 * Inventory() polls every Vsync and will catch 15 ticks, while MainLoop
	 * does not look at Fire again until AffScene has recomposed the whole
	 * 640x480 background behind it -- 461 ms, M3 -- by which time the button
	 * is long up and the modal does not immediately reopen. */
	{"inventory open",    40, 0,        F_SHIFT, 0},
	{"inventory read",    60, 0,        0,       0},
	{"inventory close",   15, 0,        F_SHIFT, 0},
	{"settle",           150, 0,        0,       0},
};

#define NB_STEPS ((int)(sizeof(script) / sizeof(script[0])))

/* Written by the tick ISR, read by the frame loop. */
static volatile int step_i;
static volatile unsigned short step_tick;
static volatile int script_done;

/*
 * Nothing happens until the game loop is actually running, and this is not a
 * nicety. The first version of this file started the script the moment the
 * timer did, and the whole of it -- twenty-five seconds of walking, turning
 * and pressing -- was delivered into a MainLoop that had not returned from
 * ChangeCube yet. The log came back with one step reported, no movement at
 * all, and the inventory sitting open on screen, which reads exactly like a
 * dead input path and was nothing of the sort.
 *
 * The 18.8-second scene load is not a pause in the tick. TimerRef counts
 * straight through it -- close to a thousand of them arrive inside one
 * ChangeCube -- so anything scheduled on the tick runs while the game is
 * blind to it. A player would see that nothing was responding and wait; the
 * script has to be told.
 *
 * PORT_M6_Frame arms it on its own first call, which is the first iteration
 * of MainLoop after the scene has composed.
 */
static volatile int armed;

/*
 * Called from PORT_ScanInput, in the 50 Hz timer interrupt.
 *
 * Returns 1 while the script is driving, having filled in the three engine
 * globals' worth of state; returns 0 once it is finished, and the caller
 * reads the real pad. Nothing here prints: this is interrupt context and the
 * TTY is a BIOS putchar per character.
 */
int PORT_M6_Script(unsigned short *joy, unsigned short *fire, unsigned short *key)
{
	const M6_STEP *s;

	if (!armed || script_done)
		return 0;

	s = &script[step_i];

	*joy = s->joy;
	*fire = s->fire;
	*key = s->key;

	if (++step_tick >= s->ticks)
	{
		step_tick = 0;
		if (++step_i >= NB_STEPS)
		{
			step_i = NB_STEPS - 1;
			script_done = 1;
		}
	}

	return 1;
}

/* ══════════════════════════════════════════════════════════════════════════
 * The report
 *
 * One line per step boundary, and the line describes the step that just
 * ENDED: what was held, and what the hero's state was before and after. A
 * step that changed nothing is a failed assertion and reads as one.
 * ═════════════════════════════════════════════════════════════════════════ */

static int reported_i = -1;
static int finished;
static int have_before;
static WORD before_x, before_y, before_z, before_beta;
static WORD before_comp;
static UBYTE before_anim;

static void snapshot(void)
{
	T_OBJET *h = &ListObjet[NUM_PERSO];

	before_x = h->PosObjX;
	before_y = h->PosObjY;
	before_z = h->PosObjZ;
	before_beta = h->Beta;
	before_anim = h->GenAnim;
	before_comp = Comportement;
	have_before = 1;
}

/*
 * Called once per iteration of MainLoop, from PORT_M5_Frame.
 *
 * The step index is advanced by the ISR, so this only has to notice that it
 * moved -- and it can move by more than one between two frames, which is
 * reported rather than papered over: a 15-tick step is 300 ms and a frame
 * during a full recompose is 461 ms.
 */
void PORT_M6_Frame(void)
{
	T_OBJET *h = &ListObjet[NUM_PERSO];
	int i = step_i;

	if (finished)
		return;

	if (!have_before)
	{
		PORT_Diag("[M6] autopilot: %d steps, hero at (%d,%d,%d) beta %d "
				  "move %d body %d comp %d\n",
				  NB_STEPS, h->PosObjX, h->PosObjY, h->PosObjZ, h->Beta,
				  h->Move, h->Body, Comportement);
		snapshot();
		reported_i = 0;
		armed = 1;
		return;
	}

	if (i == reported_i && !script_done)
		return;

	if (reported_i >= 0 && reported_i < NB_STEPS)
	{
		const M6_STEP *s = &script[reported_i];

		/* A step that took less than one frame is a step nobody measured.
		 * Say so rather than quietly reporting the wrong one. */
		if (i > reported_i + 1)
			PORT_Diag("[M6] steps %d..%d passed inside one frame, unmeasured\n",
					  reported_i + 1, i - 1);

		PORT_Diag("[M6] %2d %-16s joy %2d fire %2d key %2d | "
				  "pos %5d,%5d,%5d %+5d,%+5d,%+5d | beta %5d %+5d | "
				  "anim %d->%d comp %d->%d\n",
				  reported_i, s->name, s->joy, s->fire, s->key,
				  h->PosObjX, h->PosObjY, h->PosObjZ,
				  h->PosObjX - before_x, h->PosObjY - before_y,
				  h->PosObjZ - before_z,
				  h->Beta, h->Beta - before_beta,
				  before_anim, h->GenAnim,
				  before_comp, Comportement);
	}

	if (script_done && reported_i == i)
	{
		/* The last step's line has just gone out; say so once and stop. */
		PORT_Diag("[M6] autopilot finished -- the pad is live\n");
		finished = 1;
		return;
	}

	snapshot();
	reported_i = i;
}

#endif /* PORT_PSX_M6_AUTOPILOT */
