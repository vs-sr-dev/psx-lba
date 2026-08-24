/*
 * psx_m4.c — M4: one actor, drawn by the GPU, over the M3 scene.
 *
 * The milestone this file exists to reach is narrow and specific: Twinsen's
 * body, at his real position in the real scene, transformed by the engine's
 * own 3D pipeline and rasterised by the PlayStation's GPU instead of by
 * translate/s_fillv.c. No animation, no other actors, no frame loop.
 *
 * Everything above the rasteriser is untouched. AffObjetIso still builds the
 * display list, still culls back faces with its 1993 cross product, and still
 * sorts back to front with SergeSort; the only thing that changed is where
 * the polygons go at the bottom of it (translate/p_ob_iso.c, guarded by
 * PORT_PSX, into platform/psx/psx_poly.c).
 *
 * Unlike the rest of platform/psx, this file DOES include the engine headers.
 * It is a harness, not a shim: it has to name ListObjet, PtrBody and the
 * world origin, and inventing local copies of those declarations is exactly
 * how a port ends up with two disagreeing ideas of a structure layout.
 */

#include "watcom_compat.h"
#include "c_extern.h"

/* NOT port.h: ADELINE.H defines ULONG and friends as macros, so port.h's
 * typedefs for the same names expand into nonsense once the engine headers
 * have been seen. The handful of shim entry points this harness needs are
 * declared here instead. */
void PORT_Diag(const char *fmt, ...);
void PORT_HeapReport(const char *where);
unsigned long PORT_Micros(void);
void PORT_ActorBegin(void);
void PORT_ActorEnd(void);
void PORT_ActorStats(int *polys, int *dropped, int *lines, int *spheres);
int  PORT_ActorEmitted(void);

/*
 * Draw one object from the scene's own list. Same five lines the game loop
 * runs in OBJECT.C, deliberately: a harness that takes a shortcut here would
 * be testing the shortcut.
 */
static int DrawSceneObject(int numobj)
{
    T_OBJET *o = &ListObjet[numobj];
    UBYTE *ptrbody;
    UBYTE *ptranim;
    LONG err;
    int before;

    if (o->Body == -1 || !PtrBody[o->Body])
        return 1;                       /* a sprite, or nothing yet */

    before = PORT_ActorEmitted();

    ptrbody = PtrBody[o->Body];
    ptranim = HQR_Get(HQR_Anims, o->Anim);

    if (ptranim)
        SetInterAnimObjet2(o->Frame, ptranim, ptrbody);

    err = AffObjetIso(o->PosObjX - WorldXCube,
                      o->PosObjY - WorldYCube,
                      o->PosObjZ - WorldZCube,
                      0, o->Beta, 0,
                      ptrbody);

    if (err)
        return 1;                       /* AffObjetIso culled it all */

    /* AffObjetIso reports success when it built at least one ENTITY, which
     * it decided before any of them met the clip rectangle. An object
     * entirely off screen therefore comes back "drawn". The primitive
     * counter is the only honest answer. */
    if (PORT_ActorEmitted() == before)
        return 1;

    PORT_Diag("[M4] obj %2d: body %2d anim %3d beta %4d  box %d,%d..%d,%d  %d prims\n",
              numobj, (int)o->Body, (int)o->Anim, (int)o->Beta,
              (int)ScreenXmin, (int)ScreenYmin, (int)ScreenXmax, (int)ScreenYmax,
              PORT_ActorEmitted() - before);
    return 0;
}

/*
 * Called at the end of the M3 harness, with the background already composed
 * and already presented. The actor goes straight onto the framebuffer over
 * it — which is the architecture in one sentence: the background is an image
 * the GPU textures out of main RAM, and the actors are primitives drawn on
 * top of it, and neither one ever meets the other in the CPU.
 */
void PORT_M4_Actor(void)
{
    unsigned long t0, t1;
    int polys, dropped, lines, spheres;
    int n, drawn = 0;

    PORT_Diag("\n[M4] actors, on the GPU\n");
    PORT_Diag("[M4] %d objects in the scene, world origin %ld,%ld,%ld\n",
              (int)NbObjets, WorldXCube, WorldYCube, WorldZCube);

    UnSetClip();
    PORT_ActorBegin();

    t0 = PORT_Micros();
    for (n = 0; n < NbObjets; n++) {
        if (!DrawSceneObject(n))
            drawn++;
    }
    PORT_ActorEnd();
    t1 = PORT_Micros();

    PORT_ActorStats(&polys, &dropped, &lines, &spheres);

    PORT_Diag("[M4] %d of %d objects reached the screen, in %lu us\n",
              drawn, (int)NbObjets, t1 - t0);
    PORT_Diag("[M4] %d polygons, %d lines, %d spheres, %d dropped by the clip\n",
              polys, lines, spheres, dropped);

    PORT_HeapReport("after the actors");
}
