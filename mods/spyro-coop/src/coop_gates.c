/**
 * @file coop_gates.c
 * @brief The two collision entry guards, ported from Sp1x2Gates.c.
 *
 * WHAT THEY DEFEND. Two sibling collision routines turn a coordinate into a
 * bucket index with >> 13. Hand them a garbage position (a Y of ~13,000,000
 * was measured) and they index far past the bucket table and walk level data
 * as chain pointers: readable garbage spins forever, unaligned garbage
 * faults. On PS1 both showed up as hard freezes, and both came straight back
 * the one time the guards were retired on a theory.
 *
 * WHY PHASE A HAS THEM. A brand-new second Spyro is exactly the thing that
 * produces impossible coordinates, and a freeze during the experiment would
 * hide the answer it exists to give. Refusals are counted and shown in the
 * Mods panel; a count of zero after real play is itself worth knowing.
 *
 * THE RULE, deliberately narrow: a coordinate that is negative or at or
 * beyond 0x400000 is past any real level geometry. Refuse the query and
 * return "nothing hit", which every caller already handles. One unsigned
 * compare per axis catches both cases, since a negative reads as enormous.
 *
 * On PS1 these were entry patches that had to replay a delay-slot instruction
 * and rebuild $at by hand; a whole build was once lost to a clobbered
 * register. Here they are ordinary overrides.
 */

#include "coop.h"

#define COORD_LIMIT 0x400000u

/* Reads a guest Vector3D. Returns 0 if the pointer maps nothing, in which
   case the stock routine runs and does whatever it always did. */
static int coord_is_mad(uint32_t vec_vaddr, int* mad) {
    int32_t* v = (int32_t*)g_api->guest(vec_vaddr);
    if (!v)
        return 0;
    *mad = (uint32_t)v[0] >= COORD_LIMIT
        || (uint32_t)v[1] >= COORD_LIMIT
        || (uint32_t)v[2] >= COORD_LIMIT;
    return 1;
}

/* func_8004AE38(Vector3D* start, Vector3D* end): segment probe. The end is
   start plus offset, so checking it alone is enough. */
static void on_segment_probe(CPUState* cpu) {
    int mad = 0;
    if (coord_is_mad(cpu->a1, &mad) && mad) {
        g_stats.probe_refusals++;
        cpu->v0 = 0;                         /* "nothing hit" */
        return;
    }
    g_api->base(cpu);
}

/* func_8004BE4C(Vector3D* pos, int radiusMobys, int radiusTerrain): sphere
   query. */
static void on_sphere_query(CPUState* cpu) {
    int mad = 0;
    if (coord_is_mad(cpu->a0, &mad) && mad) {
        g_stats.query_refusals++;
        cpu->v0 = 0;
        return;
    }
    g_api->base(cpu);
}

int coop_gates_install(void) {
    if (g_api->override_name(g_self, "func_8004AE38", on_segment_probe) != 0 ||
        g_api->override_name(g_self, "func_8004BE4C", on_sphere_query) != 0) {
        coop_log(OP_MOD_LOG_ERROR, "could not install collision guards");
        return 1;
    }
    return 0;
}
