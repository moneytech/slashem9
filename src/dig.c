/*	SCCS Id: @(#)dig.c	3.4	2003/03/23	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

/* #define DEBUG */ /* turn on for diagnostics */

static bool did_dig_msg;

static bool rm_waslit(void);
static void mkcavepos(xchar, xchar, int, boolean, boolean);
static void mkcavearea(boolean);
static int dig_typ(struct obj *, xchar, xchar);
static int dig(void);
static void dig_up_grave(void);

/* Indices returned by dig_typ() */
#define DIGTYP_UNDIGGABLE 0
#define DIGTYP_ROCK	  1
#define DIGTYP_STATUE	  2
#define DIGTYP_BOULDER	  3
#define DIGTYP_DOOR	  4
#define DIGTYP_TREE	  5

static bool rm_waslit(void) {
	xchar x, y;

	if (levl[u.ux][u.uy].typ == ROOM && levl[u.ux][u.uy].waslit)
		return true;
	for (x = u.ux - 2; x < u.ux + 3; x++)
		for (y = u.uy - 1; y < u.uy + 2; y++)
			if (isok(x, y) && levl[x][y].waslit) return true;
	return false;
}

/* Change level topology.  Messes with vision tables and ignores things like
 * boulders in the name of a nice effect.  Vision will get fixed up again
 * immediately after the effect is complete.
 */
static void mkcavepos(xchar x, xchar y, int dist, boolean waslit, boolean rockit) {
	struct rm *lev;

	if (!isok(x, y)) return;
	lev = &levl[x][y];

	if (rockit) {
		struct monst *mtmp;

		if (IS_ROCK(lev->typ)) return;
		if (t_at(x, y)) return;	      /* don't cover the portal */
		if ((mtmp = m_at(x, y)) != 0) /* make sure crucial monsters survive */
			if (!passes_walls(mtmp->data)) rloc(mtmp, false);
	} else if (lev->typ == ROOM)
		return;

	unblock_point(x, y); /* make sure vision knows this location is open */

	/* fake out saved state */
	lev->seenv = 0;
	lev->doormask = 0;
	if (dist < 3) lev->lit = (rockit ? false : true);
	if (waslit) lev->waslit = (rockit ? false : true);
	lev->horizontal = false;
	viz_array[y][x] = (dist < 3) ?
				  (IN_SIGHT | COULD_SEE) : /* short-circuit vision recalc */
				  COULD_SEE;
	lev->typ = (rockit ? STONE : ROOM);
	if (dist >= 3)
		impossible("mkcavepos called with dist %d", dist);
	if (Blind)
		feel_location(x, y);
	else
		newsym(x, y);
}

static void mkcavearea(boolean rockit) {
	int dist;
	xchar xmin = u.ux, xmax = u.ux;
	xchar ymin = u.uy, ymax = u.uy;
	xchar i;
	boolean waslit = rm_waslit();

	if (rockit)
		pline("Crash!  The ceiling collapses around you!");
	else
		pline("A mysterious force %s cave around you!",
		      (levl[u.ux][u.uy].typ == CORR) ? "creates a" : "extends the");
	display_nhwindow(WIN_MESSAGE, true);

	for (dist = 1; dist <= 2; dist++) {
		xmin--;
		xmax++;

		/* top and bottom */
		if (dist < 2) { /* the area is wider that it is high */
			ymin--;
			ymax++;
			for (i = xmin + 1; i < xmax; i++) {
				mkcavepos(i, ymin, dist, waslit, rockit);
				mkcavepos(i, ymax, dist, waslit, rockit);
			}
		}

		/* left and right */
		for (i = ymin; i <= ymax; i++) {
			mkcavepos(xmin, i, dist, waslit, rockit);
			mkcavepos(xmax, i, dist, waslit, rockit);
		}

		flush_screen(1); /* make sure the new glyphs shows up */
		delay_output();
	}

	if (!rockit && levl[u.ux][u.uy].typ == CORR) {
		levl[u.ux][u.uy].typ = ROOM;
		if (waslit) levl[u.ux][u.uy].waslit = true;
		newsym(u.ux, u.uy); /* in case player is invisible */
	}

	vision_full_recalc = 1; /* everything changed */
}

/* When digging into location <x,y>, what are you actually digging into? */
static int dig_typ(struct obj *otmp, xchar x, xchar y) {
	boolean ispick = is_pick(otmp);

	return (ispick && sobj_at(STATUE, x, y) ? DIGTYP_STATUE :
						  ispick && sobj_at(BOULDER, x, y) ? DIGTYP_BOULDER :
										     closed_door(x, y) ? DIGTYP_DOOR :
													 IS_TREE(levl[x][y].typ) ?
													 (ispick ? DIGTYP_UNDIGGABLE : DIGTYP_TREE) :
													 ispick && IS_ROCK(levl[x][y].typ) &&
																 (!level.flags.arboreal || IS_WALL(levl[x][y].typ)) ?
													 DIGTYP_ROCK :
													 DIGTYP_UNDIGGABLE);
}

boolean is_digging(void) {
	if (occupation == dig) {
		return true;
	}
	return false;
}

#define BY_YOU	  (&youmonst)
#define BY_OBJECT (NULL)

boolean dig_check(struct monst *madeby, boolean verbose, int x, int y) {
	struct trap *ttmp = t_at(x, y);
	const char *verb =
		(madeby != BY_YOU || !uwep || is_pick(uwep)) ? "dig in" :
							       is_lightsaber(uwep) ? "cut" : "chop";

	if (On_stairs(x, y)) {
		if (x == xdnladder || x == xupladder) {
			if (verbose) pline("The ladder resists your effort.");
		} else if (verbose)
			pline("The stairs are too hard to %s.", verb);
		return false;
		/* ALI - Artifact doors */
	} else if (IS_DOOR(levl[x][y].typ) && artifact_door(x, y)) {
		if (verbose) pline("The %s here is too hard to dig in.",
				   surface(x, y));
		return false;
	} else if (IS_THRONE(levl[x][y].typ) && madeby != BY_OBJECT) {
		if (verbose) pline("The throne is too hard to break apart.");
		return false;
	} else if (IS_ALTAR(levl[x][y].typ) && (madeby != BY_OBJECT ||
						Is_astralevel(&u.uz) || Is_sanctum(&u.uz))) {
		if (verbose) pline("The altar is too hard to break apart.");
		return false;
	} else if (Is_airlevel(&u.uz)) {
		if (verbose) pline("You cannot %s thin air.", verb);
		return false;
	} else if (Is_waterlevel(&u.uz)) {
		if (verbose) pline("The water splashes and subsides.");
		return false;
	} else if ((IS_ROCK(levl[x][y].typ) && levl[x][y].typ != SDOOR &&
		    (levl[x][y].wall_info & W_NONDIGGABLE) != 0) ||
		   (ttmp &&
		    (ttmp->ttyp == MAGIC_PORTAL || (!Can_dig_down(&u.uz) && !levl[x][y].candig)))) {
		if (verbose) pline("The %s here is too hard to %s.",
				   surface(x, y), verb);
		return false;
	} else if (sobj_at(BOULDER, x, y)) {
		if (verbose) pline("There isn't enough room to %s here.", verb);
		return false;
	} else if (madeby == BY_OBJECT &&
		   /* the block against existing traps is mainly to
	                   prevent broken wands from turning holes into pits */
		   (ttmp || is_pool(x, y) || is_lava(x, y))) {
		/* digging by player handles pools separately */
		return false;
	}
	return true;
}

static int dig(void) {
	struct rm *lev;
	xchar dpx = context.digging.pos.x, dpy = context.digging.pos.y;
	boolean ispick = uwep && is_pick(uwep);
	const char *verb =
		(!uwep || is_pick(uwep)) ? "dig into" :
					   is_lightsaber(uwep) ? "cut through" :
								 "chop through";
	int bonus;

	lev = &levl[dpx][dpy];
	/* perhaps a nymph stole your pick-axe while you were busy digging */
	/* or perhaps you teleported away */
	/* WAC allow lightsabers */
	if (u.uswallow || !uwep || (!ispick && (!is_lightsaber(uwep) || !uwep->lamplit) && !is_axe(uwep)) ||
	    !on_level(&context.digging.level, &u.uz) ||
	    ((context.digging.down ? (dpx != u.ux || dpy != u.uy) : (distu(dpx, dpy) > 2))))
		return 0;

	if (context.digging.down) {
		if (!dig_check(BY_YOU, true, u.ux, u.uy)) return 0;
	} else { /* !context.digging.down */
		if (IS_TREE(lev->typ) && !may_dig(dpx, dpy) &&
		    dig_typ(uwep, dpx, dpy) == DIGTYP_TREE) {
			pline("This tree seems to be petrified.");
			return 0;
		}
		/* ALI - Artifact doors */
		if ((IS_ROCK(lev->typ) && !may_dig(dpx, dpy) &&
		     dig_typ(uwep, dpx, dpy) == DIGTYP_ROCK) ||
		    (IS_DOOR(lev->typ) && artifact_door(dpx, dpy))) {
			pline("This %s is too hard to %s.",
			      IS_DOOR(lev->typ) ? "door" : "wall", verb);
			return 0;
		}
	}
	if (Fumbling &&
	    /* Can't exactly miss holding a lightsaber to the wall */
	    !is_lightsaber(uwep) &&
	    !rn2(3)) {
		switch (rn2(3)) {
			case 0:
				if (!welded(uwep)) {
					pline("You fumble and drop %s.", yname(uwep));
					dropx(uwep);
				} else {
					if (u.usteed)
						pline("%s and %s %s!", Yobjnam2(uwep, "bounce"), otense(uwep, "hit"), mon_nam(u.usteed));
					else
						pline("Ouch!  %s and %s you!", Yobjnam2(uwep, "bounce"), otense(uwep, "hit"));
					set_wounded_legs(RIGHT_SIDE, 5 + rnd(5));
				}
				break;
			case 1:
				pline("Bang!  You hit with the broad side of %s!",
				      the(xname(uwep)));
				break;
			default:
				pline("Your swing misses its mark.");
				break;
		}
		return 0;
	}

	bonus = 10 + rn2(5) + abon() + uwep->spe - greatest_erosion(uwep) + u.udaminc;
	if (Race_if(PM_DWARF))
		bonus *= 2;

	if (is_lightsaber(uwep))
		bonus -= rn2(20); /* Melting a hole takes longer */

	context.digging.effort += bonus;

	if (context.digging.down) {
		struct trap *ttmp = t_at(dpx, dpy);

		if (context.digging.effort > 250 || (ttmp && ttmp->ttyp == HOLE)) {
			dighole(false);
			memset((void *)&context.digging, 0, sizeof context.digging);
			return 0; /* done with digging */
		}

		if (context.digging.effort <= 50 || is_lightsaber(uwep)
		    || ((ttmp = t_at(dpx, dpy)) != 0 && (is_pitlike(ttmp->ttyp) || ttmp->ttyp == TRAPDOOR)))

			return 1;

		if (IS_ALTAR(lev->typ)) {
			altar_wrath(dpx, dpy);
			angry_priest();
		}

		if (dighole(true)) { /* make pit at <u.ux,u.uy> */
			context.digging.level.dnum = 0;
			context.digging.level.dlevel = -1;
		}
		return 0;
	}

	if (context.digging.effort > 100) {
		const char *digtxt, *dmgtxt = NULL;
		struct obj *obj;
		boolean shopedge = *in_rooms(dpx, dpy, SHOPBASE);

		if ((obj = sobj_at(STATUE, dpx, dpy)) != 0) {
			if (break_statue(obj))
				digtxt = "The statue shatters.";
			else
				/* it was a statue trap; break_statue()
				 * printed a message and updated the screen
				 */
				digtxt = NULL;
		} else if ((obj = sobj_at(BOULDER, dpx, dpy)) != 0) {
			struct obj *bobj;

			fracture_rock(obj);
			if ((bobj = sobj_at(BOULDER, dpx, dpy)) != 0) {
				/* another boulder here, restack it to the top */
				obj_extract_self(bobj);
				place_object(bobj, dpx, dpy);
			}
			digtxt = "The boulder falls apart.";
		} else if (lev->typ == STONE || lev->typ == SCORR ||
			   IS_TREE(lev->typ)) {
			if (Is_earthlevel(&u.uz)) {
				if (uwep->blessed && !rn2(3)) {
					mkcavearea(false);
					goto cleanup;
				} else if ((uwep->cursed && !rn2(4)) ||
					   (!uwep->blessed && !rn2(6))) {
					mkcavearea(true);
					goto cleanup;
				}
			}
			if (IS_TREE(lev->typ)) {
				digtxt = "You cut down the tree.";
				lev->typ = ROOM;
				if (!rn2(5)) rnd_treefruit_at(dpx, dpy);
			} else {
				digtxt = "You succeed in cutting away some rock.";
				lev->typ = CORR;
			}
		} else if (IS_WALL(lev->typ)) {
			if (shopedge) {
				add_damage(dpx, dpy, 10L * ACURRSTR);
				dmgtxt = "damage";
			}
			if (level.flags.is_maze_lev) {
				lev->typ = ROOM;
			} else if (level.flags.is_cavernous_lev &&
				   !in_town(dpx, dpy)) {
				lev->typ = CORR;
			} else {
				lev->typ = DOOR;
				lev->doormask = D_NODOOR;
			}
			digtxt = "You make an opening in the wall.";
		} else if (lev->typ == SDOOR) {
			cvt_sdoor_to_door(lev); /* ->typ = DOOR */
			digtxt = "You break through a secret door!";
			if (!(lev->doormask & D_TRAPPED))
				lev->doormask = D_BROKEN;
		} else if (closed_door(dpx, dpy)) {
			digtxt = "You break through the door.";
			if (shopedge) {
				add_damage(dpx, dpy, 400L);
				dmgtxt = "break";
			}
			if (!(lev->doormask & D_TRAPPED))
				lev->doormask = D_BROKEN;
		} else
			return 0; /* statue or boulder got taken */

		if (!does_block(dpx, dpy, &levl[dpx][dpy]))
			unblock_point(dpx, dpy); /* vision:  can see through */
		if (Blind)
			feel_location(dpx, dpy);
		else
			newsym(dpx, dpy);
		if (digtxt && !context.digging.quiet) pline("%s", digtxt); /* after newsym */
		if (dmgtxt)
			pay_for_damage(dmgtxt, false);

		if (Is_earthlevel(&u.uz) && !rn2(3)) {
			struct monst *mtmp;

			switch (rn2(2)) {
				case 0:
					mtmp = makemon(&mons[PM_EARTH_ELEMENTAL],
						       dpx, dpy, NO_MM_FLAGS);
					break;
				default:
					mtmp = makemon(&mons[PM_XORN],
						       dpx, dpy, NO_MM_FLAGS);
					break;
			}
			if (mtmp) pline("The debris reassembles and comes to life!");
		}
		if (IS_DOOR(lev->typ) && (lev->doormask & D_TRAPPED)) {
			lev->doormask = D_NODOOR;
			b_trapped("door", 0);
			newsym(dpx, dpy);
		}
	cleanup:
		context.digging.lastdigtime = moves;
		context.digging.quiet = false;
		context.digging.level.dnum = 0;
		context.digging.level.dlevel = -1;
		return 0;
	} else { /* not enough effort has been spent yet */
		static const char *const d_target[6] = {
			"", "rock", "statue", "boulder", "door", "tree"};
		int dig_target = dig_typ(uwep, dpx, dpy);

		if (IS_WALL(lev->typ) || dig_target == DIGTYP_DOOR) {
			if (*in_rooms(dpx, dpy, SHOPBASE)) {
				pline("This %s seems too hard to %s.",
				      IS_DOOR(lev->typ) ? "door" : "wall", verb);
				return 0;
			}
		} else if (!IS_ROCK(lev->typ) && dig_target == DIGTYP_ROCK)
			return 0; /* statue or boulder got taken */
		if (!did_dig_msg) {
			if (is_lightsaber(uwep))
				pline("You burn steadily through %s.", the(d_target[dig_target]));
			else
				pline("You hit the %s with all your might.", d_target[dig_target]);
			did_dig_msg = true;
		}
	}
	return 1;
}

/* When will hole be finished? Very rough indication used by shopkeeper. */
int holetime(void) {
	if (occupation != dig || !*u.ushops) return -1;
	return (250 - context.digging.effort) / 20;
}

/* Return typ of liquid to fill a hole with, or ROOM, if no liquid nearby */
schar fillholetyp(int x, int y) {
	int x1, y1;
	int lo_x = max(1, x - 1), hi_x = min(x + 1, COLNO - 1),
	    lo_y = max(0, y - 1), hi_y = min(y + 1, ROWNO - 1);
	int pool_cnt = 0, moat_cnt = 0, lava_cnt = 0;

	for (x1 = lo_x; x1 <= hi_x; x1++)
		for (y1 = lo_y; y1 <= hi_y; y1++)
			if (levl[x1][y1].typ == POOL)
				pool_cnt++;
			else if (levl[x1][y1].typ == MOAT ||
				 (levl[x1][y1].typ == DRAWBRIDGE_UP &&
				  (levl[x1][y1].drawbridgemask & DB_UNDER) == DB_MOAT))
				moat_cnt++;
			else if (levl[x1][y1].typ == LAVAPOOL ||
				 (levl[x1][y1].typ == DRAWBRIDGE_UP &&
				  (levl[x1][y1].drawbridgemask & DB_UNDER) == DB_LAVA))
				lava_cnt++;
	pool_cnt /= 3; /* not as much liquid as the others */

	if (lava_cnt > moat_cnt + pool_cnt && rn2(lava_cnt + 1))
		return LAVAPOOL;
	else if (moat_cnt > 0 && rn2(moat_cnt + 1))
		return MOAT;
	else if (pool_cnt > 0 && rn2(pool_cnt + 1))
		return POOL;
	else
		return ROOM;
}

void digactualhole(int x, int y, struct monst *madeby, int ttyp) {
	struct obj *oldobjs, *newobjs;
	struct trap *ttmp;
	char surface_type[BUFSZ];
	struct rm *lev = &levl[x][y];
	boolean shopdoor;
	struct monst *mtmp = m_at(x, y); /* may be madeby */
	boolean madeby_u = (madeby == BY_YOU);
	boolean madeby_obj = (madeby == BY_OBJECT);
	boolean at_u = (x == u.ux) && (y == u.uy);
	boolean wont_fall = Levitation || Flying;

	if (u.utrap && u.utraptype == TT_INFLOOR) u.utrap = 0;

	/* these furniture checks were in dighole(), but wand
	   breaking bypasses that routine and calls us directly */
	if (IS_FOUNTAIN(lev->typ)) {
		dogushforth(false);
		SET_FOUNTAIN_WARNED(x, y); /* force dryup */
		dryup(x, y, madeby_u);
		return;
	} else if (IS_SINK(lev->typ)) {
		breaksink(x, y);
		return;
	} else if (IS_TOILET(lev->typ)) {
		breaktoilet(u.ux, u.uy);
	} else if (lev->typ == DRAWBRIDGE_DOWN ||
		   (is_drawbridge_wall(x, y) >= 0)) {
		int bx = x, by = y;
		/* if under the portcullis, the bridge is adjacent */
		find_drawbridge(&bx, &by);
		destroy_drawbridge(bx, by);
		return;
	}

	if (ttyp != PIT && !Can_dig_down(&u.uz) && !lev->candig) {
		impossible("digactualhole: can't dig %s on this level.",
			   sym_desc[trap_to_defsym(ttyp)].explanation);
		ttyp = PIT;
	}

	/* maketrap() might change it, also, in this situation,
	   surface() returns an inappropriate string for a grave */
	if (IS_GRAVE(lev->typ))
		strcpy(surface_type, "grave");
	else
		strcpy(surface_type, surface(x, y));
	shopdoor = IS_DOOR(lev->typ) && *in_rooms(x, y, SHOPBASE);
	oldobjs = level.objects[x][y];
	ttmp = maketrap(x, y, ttyp);
	if (!ttmp) return;
	newobjs = level.objects[x][y];
	ttmp->madeby_u = madeby_u;
	if (cansee(x, y)) seetrap(ttmp);
	else if (madeby_u) feeltrap(ttmp);

	if (ttyp == PIT) {
		if (madeby_u) {
			pline("You dig a pit in the %s.", surface_type);
			if (shopdoor) pay_for_damage("ruin", false);
		} else if (!madeby_obj && canseemon(madeby))
			pline("%s digs a pit in the %s.", Monnam(madeby), surface_type);
		else if (cansee(x, y) && flags.verbose)
			pline("A pit appears in the %s.", surface_type);

		if (at_u) {
			if (!wont_fall) {
				u.utrap = rn1(4, 2);
				u.utraptype = TT_PIT;
				vision_full_recalc = 1; /* vision limits change */
			} else
				u.utrap = 0;
			if (oldobjs != newobjs) /* something unearthed */
				pickup(1);	/* detects pit */
		} else if (mtmp) {
			if (is_flyer(mtmp->data) || is_floater(mtmp->data)) {
				if (canseemon(mtmp))
					pline("%s %s over the pit.", Monnam(mtmp),
					      (is_flyer(mtmp->data)) ?
						      "flies" :
						      "floats");
			} else if (mtmp != madeby)
				mintrap(mtmp);
		}
	} else { /* was TRAPDOOR now a HOLE*/

		if (madeby_u)
			pline("You dig a hole through the %s.", surface_type);
		else if (!madeby_obj && canseemon(madeby))
			pline("%s digs a hole through the %s.",
			      Monnam(madeby), surface_type);
		else if (cansee(x, y) && flags.verbose)
			pline("A hole appears in the %s.", surface_type);

		if (at_u) {
			if (!u.ustuck && !wont_fall && !next_to_u()) {
				pline("You are jerked back by your pet!");
				wont_fall = true;
			}

			/* Floor objects get a chance of falling down.  The case where
			 * the hero does NOT fall down is treated here.  The case
			 * where the hero does fall down is treated in goto_level().
			 */
			if (u.ustuck || wont_fall) {
				if (newobjs)
					impact_drop(NULL, x, y, 0);
				if (oldobjs != newobjs)
					pickup(1);
				if (shopdoor && madeby_u) pay_for_damage("ruin", false);

			} else {
				d_level newlevel;
				const char *You_fall = "You fall through...";

				if (*u.ushops && madeby_u)
					shopdig(1); /* shk might snatch pack */
				/* handle earlier damage, eg breaking wand of digging */
				else if (!madeby_u)
					pay_for_damage("dig into", true);

				/* Earlier checks must ensure that the destination
				 * level exists and is in the present dungeon.
				 */
				newlevel.dnum = u.uz.dnum;
				newlevel.dlevel = u.uz.dlevel + 1;
				/* Cope with holes caused by monster's actions -- ALI */
				if (context.mon_moving) {
					schedule_goto(&newlevel, false, true, false,
						      You_fall, NULL);
				} else {
					pline("%s", You_fall);
					goto_level(&newlevel, false, true, false);
					/* messages for arriving in special rooms */
					spoteffects(false);
				}
			}
		} else {
			if (shopdoor && madeby_u) pay_for_damage("ruin", false);
			if (newobjs)
				impact_drop(NULL, x, y, 0);
			if (mtmp) {
				/*[don't we need special sokoban handling here?]*/
				if (is_flyer(mtmp->data) || is_floater(mtmp->data) ||
				    mtmp->data == &mons[PM_WUMPUS] ||
				    (mtmp->wormno && count_wsegs(mtmp) > 5) ||
				    mtmp->data->msize >= MZ_HUGE) return;
				if (mtmp == u.ustuck) /* probably a vortex */
					return;	      /* temporary? kludge */

				if (teleport_pet(mtmp, false)) {
					d_level tolevel;

					if (Is_stronghold(&u.uz)) {
						assign_level(&tolevel, &valley_level);
					} else if (Is_botlevel(&u.uz)) {
						if (canseemon(mtmp))
							pline("%s avoids the trap.", Monnam(mtmp));
						return;
					} else {
						get_level(&tolevel, depth(&u.uz) + 1);
					}
					if (mtmp->isshk) make_angry_shk(mtmp, 0, 0);
					migrate_to_level(mtmp, ledger_no(&tolevel),
							 MIGR_RANDOM, NULL);
				}
			}
		}
	}
}

/*
 * Called from dighole(), but also from do_break_wand()
 * in apply.c.
 */
void liquid_flow(xchar x, xchar y, schar typ, struct trap *ttmp, const char *fillmsg) {
	bool u_spot = (x == u.ux && y == u.uy);

	if (ttmp) delfloortrap(ttmp);
	/* if any objects were frozen here, they're released now */
	unearth_objs(x, y);

	if (fillmsg) pline(fillmsg, typ == LAVAPOOL ? "lava" : "water");
	if (u_spot && !(Levitation || Flying)) {
		if (typ == LAVAPOOL)
			lava_effects();
		else if (!Wwalking && !Swimming)
			drown();
	}
}


/* return true if digging succeeded, false otherwise */
boolean dighole(boolean pit_only) {
	struct trap *ttmp = t_at(u.ux, u.uy);
	struct rm *lev = &levl[u.ux][u.uy];
	struct obj *boulder_here;
	schar typ;
	bool nohole = !Can_dig_down(&u.uz) && !lev->candig;

	if ((ttmp && (ttmp->ttyp == MAGIC_PORTAL || nohole)) ||
	    /* ALI - artifact doors */
	    (IS_DOOR(levl[u.ux][u.uy].typ) && artifact_door(u.ux, u.uy)) ||
	    (IS_ROCK(lev->typ) && lev->typ != SDOOR &&
	     (lev->wall_info & W_NONDIGGABLE) != 0)) {
		pline("The %s here is too hard to dig in.", surface(u.ux, u.uy));

	} else if (is_pool(u.ux, u.uy) || is_lava(u.ux, u.uy)) {
		pline("The %s sloshes furiously for a moment, then subsides.",
		      is_lava(u.ux, u.uy) ? "lava" : "water");
		wake_nearby(); /* splashing */

	} else if (lev->typ == DRAWBRIDGE_DOWN ||
		   (is_drawbridge_wall(u.ux, u.uy) >= 0)) {
		/* drawbridge_down is the platform crossing the moat when the
		   bridge is extended; drawbridge_wall is the open "doorway" or
		   closed "door" where the portcullis/mechanism is located */
		if (pit_only) {
			pline("The drawbridge seems too hard to dig through.");
			return false;
		} else if (IS_GRAVE(lev->typ)) {
			digactualhole(u.ux, u.uy, BY_YOU, PIT);
			dig_up_grave();
			return true;
		} else {
			int x = u.ux, y = u.uy;
			/* if under the portcullis, the bridge is adjacent */
			find_drawbridge(&x, &y);
			destroy_drawbridge(x, y);
			return true;
		}

	} else if ((boulder_here = sobj_at(BOULDER, u.ux, u.uy)) != 0) {
		if (ttmp && is_pitlike(ttmp->ttyp) && rn2(2)) {
			pline("The boulder settles into the pit.");
			ttmp->ttyp = PIT; /* crush spikes */
		} else {
			/*
			 * digging makes a hole, but the boulder immediately
			 * fills it.  Final outcome:  no hole, no boulder.
			 */
			pline("KADOOM! The boulder falls in!");
			delfloortrap(ttmp);
		}
		delobj(boulder_here);
		return true;

	} else if (IS_GRAVE(lev->typ)) {
		dig_up_grave();
		digactualhole(u.ux, u.uy, BY_YOU, PIT);
		return true;
	} else if (lev->typ == DRAWBRIDGE_UP) {
		/* must be floor or ice, other cases handled above */
		/* dig "pit" and let fluid flow in (if possible) */
		typ = fillholetyp(u.ux, u.uy);

		if (typ == ROOM) {
			/*
			 * We can't dig a hole here since that will destroy
			 * the drawbridge.  The following is a cop-out. --dlc
			 */
			pline("The %s here is too hard to dig in.",
			      surface(u.ux, u.uy));
			return false;
		}

		lev->drawbridgemask &= ~DB_UNDER;
		lev->drawbridgemask |= (typ == LAVAPOOL) ? DB_LAVA : DB_MOAT;

		liquid_flow(u.ux, u.uy, typ, ttmp, "As you dig, the hole fills with %s!");
		return true;

		/* the following two are here for the wand of digging */
	} else if (IS_THRONE(lev->typ)) {
		pline("The throne is too hard to break apart.");

	} else if (IS_ALTAR(lev->typ)) {
		pline("The altar is too hard to break apart.");

	} else {
		typ = fillholetyp(u.ux, u.uy);

		if (typ != ROOM) {
			lev->typ = typ;
			liquid_flow(u.ux, u.uy, typ, ttmp, "As you dig, the hole fills with %s!");
			return true;
		}

		/* finally we get to make a hole */
		if (nohole || pit_only)
			digactualhole(u.ux, u.uy, BY_YOU, PIT);
		else
			digactualhole(u.ux, u.uy, BY_YOU, HOLE);

		return true;
	}

	return false;
}

static void dig_up_grave(void) {
	struct obj *otmp;

	/* Grave-robbing is frowned upon... */
	exercise(A_WIS, false);
	if (Role_if(PM_ARCHEOLOGIST)) {
		adjalign(-sgn(u.ualign.type) * 3);
		pline("You feel like a despicable grave-robber!");
	} else if (Role_if(PM_SAMURAI)) {
		adjalign(-sgn(u.ualign.type));
		pline("You disturb the honorable dead!");
	} else if ((u.ualign.type == A_LAWFUL) && (u.ualign.record > -10)) {
		adjalign(-sgn(u.ualign.type));
		pline("You have violated the sanctity of this grave!");
	}

	switch (rn2(5)) {
		case 0:
		case 1:
			pline("You unearth a corpse.");
			if (!!(otmp = mk_tt_object(CORPSE, u.ux, u.uy)))
				otmp->age -= 100; /* this is an *OLD* corpse */
			;
			break;
		case 2:
			if (!Blind) pline(Hallucination ? "Dude!  The living dead!" :
							  "The grave's owner is very upset!");
			makemon(mkclass(S_ZOMBIE, 0), u.ux, u.uy, NO_MM_FLAGS);
			break;
		case 3:
			if (!Blind) pline(Hallucination ? "I want my mummy!" :
							  "You've disturbed a tomb!");
			makemon(mkclass(S_MUMMY, 0), u.ux, u.uy, NO_MM_FLAGS);
			break;
		default:
			/* No corpse */
			pline("The grave seems unused.  Strange....");
			break;
	}
	levl[u.ux][u.uy].typ = ROOM;
	del_engr_at(u.ux, u.uy);
	newsym(u.ux, u.uy);
	return;
}

int use_pick_axe(struct obj *obj) {
	boolean ispick;
	char dirsyms[12];
	char qbuf[QBUFSZ];
	char *dsp = dirsyms;
	int rx, ry;
	int res = 0;
	const char *sdp, *verb;

	if (iflags.num_pad)
		sdp = ndir;
	else
		sdp = sdir; /* DICE workaround */

	/* Check tool */
	if (obj != uwep) {
		if (!wield_tool(obj, "swing"))
			return 0;
		else
			res = 1;
	}
	ispick = is_pick(obj);
	verb = ispick ? "dig" : "chop";

	if (u.utrap && u.utraptype == TT_WEB) {
		pline("%s you can't %s while entangled in a web.",
		      /* res==0 => no prior message;
		         res==1 => just got "You now wield a pick-axe." message */
		      !res ? "Unfortunately," : "But", verb);
		return res;
	}

	while (*sdp) {
		movecmd(*sdp); /* sets u.dx and u.dy and u.dz */
		rx = u.ux + u.dx;
		ry = u.uy + u.dy;
		/* Include down even with axe, so we have at least one direction */
		if (u.dz > 0 ||
		    (u.dz == 0 && isok(rx, ry) &&
		     dig_typ(obj, rx, ry) != DIGTYP_UNDIGGABLE))
			*dsp++ = *sdp;
		sdp++;
	}
	*dsp = 0;
	sprintf(qbuf, "In what direction do you want to %s? [%s]", verb, dirsyms);
	if (!getdir(qbuf))
		return res;

	return use_pick_axe2(obj);
}

/* general dig through doors/etc. function
 * Handles pickaxes/lightsabers/axes
 * called from doforce and use_pick_axe
 */

/* MRKR: use_pick_axe() is split in two to allow autodig to bypass */
/*       the "In what direction do you want to dig?" query.        */
/*       use_pick_axe2() uses the existing u.dx, u.dy and u.dz    */

int use_pick_axe2(struct obj *obj) {
	int rx, ry;
	struct rm *lev;
	int dig_target, digtyp;
	boolean ispick = is_pick(obj);
	const char *verbing = ispick ? "digging" :
				       is_lightsaber(uwep) ? "cutting" :
							     "chopping";

	/* 0 = pick, 1 = lightsaber, 2 = axe */
	digtyp = (is_pick(uwep) ? 0 :
				  is_lightsaber(uwep) ? 1 : 2);

	if (u.uswallow && attack(u.ustuck)) {
		; /* return(1) */
	} else if (Underwater) {
		pline("Turbulence torpedoes your %s attempts.", verbing);
	} else if (u.dz < 0) {
		if (Levitation)
			if (digtyp == 1)
				pline("The ceiling is too hard to cut through.");
			else
				pline("You don't have enough leverage.");
		else
			pline("You can't reach the %s.", ceiling(u.ux, u.uy));
	} else if (!u.dx && !u.dy && !u.dz) {
		/* NOTREACHED for lightsabers/axes called from doforce */

		char buf[BUFSZ];
		int dam;

		dam = rnd(2) + dbon() + obj->spe;
		if (dam <= 0) dam = 1;
		pline("You hit yourself with %s.", yname(uwep));
		sprintf(buf, "%s own %s", uhis(),
			OBJ_NAME(objects[obj->otyp]));
		losehp(Maybe_Half_Phys(dam), buf, KILLED_BY);
		context.botl = 1;
		return 1;
	} else if (u.dz == 0) {
		if (Stunned || (Confusion && !rn2(5))) confdir();
		rx = u.ux + u.dx;
		ry = u.uy + u.dy;
		if (!isok(rx, ry)) {
			if (digtyp == 1)
				pline("Your %s bounces off harmlessly.",
				      aobjnam(obj, NULL));
			else
				pline("Clash!");
			return 1;
		}
		lev = &levl[rx][ry];
		if (MON_AT(rx, ry) && attack(m_at(rx, ry)))
			return 1;
		dig_target = dig_typ(obj, rx, ry);
		if (dig_target == DIGTYP_UNDIGGABLE) {
			/* ACCESSIBLE or POOL */
			struct trap *trap = t_at(rx, ry);

			if (trap && trap->ttyp == WEB) {
				if (!trap->tseen) {
					seetrap(trap);
					pline("There is a spider web there!");
				}
				pline("%s entangled in the web.", Yobjnam2(obj, "become"));
				/* you ought to be able to let go; tough luck */
				/* (maybe `move_into_trap()' would be better) */
				nomul(-d(2, 2));
				nomovemsg = "You pull free.";
			} else if (lev->typ == IRONBARS) {
				pline("Clang!");
				wake_nearby();
			} else if (IS_TREE(lev->typ))
				pline("You need an axe to cut down a tree.");
			else if (IS_ROCK(lev->typ))
				pline("You need a pick to dig rock.");
			else if (!ispick && (sobj_at(STATUE, rx, ry) ||
					     sobj_at(BOULDER, rx, ry))) {
				boolean vibrate = !rn2(3);
				pline("Sparks fly as you whack the %s.%s",
				      sobj_at(STATUE, rx, ry) ? "statue" : "boulder",
				      vibrate ? " The axe-handle vibrates violently!" : "");
				if (vibrate) losehp(Maybe_Half_Phys(2), "axing a hard object", KILLED_BY);
			} else
				pline("You swing %s through thin air.", yobjnam(obj, NULL));
		} else {
			static const char *const d_action[6][2] = {
				{"swinging", "slicing the air"},
				{"digging", "cutting through the wall"},
				{"chipping the statue", "cutting the statue"},
				{"hitting the boulder", "cutting through the boulder"},
				{"chopping at the door", "burning through the door"},
				{"cutting the tree", "razing the tree"}};
			did_dig_msg = false;
			context.digging.quiet = false;
			if (context.digging.pos.x != rx || context.digging.pos.y != ry ||
			    !on_level(&context.digging.level, &u.uz) || context.digging.down) {
				if (flags.autodig &&
				    dig_target == DIGTYP_ROCK && !context.digging.down &&
				    context.digging.pos.x == u.ux &&
				    context.digging.pos.y == u.uy &&
				    (moves <= context.digging.lastdigtime + 2 &&
				     moves >= context.digging.lastdigtime)) {
					/* avoid messages if repeated autodigging */
					did_dig_msg = true;
					context.digging.quiet = true;
				}
				context.digging.down = context.digging.chew = false;
				context.digging.warned = false;
				context.digging.pos.x = rx;
				context.digging.pos.y = ry;
				assign_level(&context.digging.level, &u.uz);
				context.digging.effort = 0;
				if (!context.digging.quiet)
					pline("You start %s.", d_action[dig_target][digtyp == 1]);
			} else {
				pline("You %s %s.", context.digging.chew ? "begin" : "continue",
				      d_action[dig_target][digtyp == 1]);
				context.digging.chew = false;
			}
			set_occupation(dig, verbing, 0);
		}
	} else if (Is_airlevel(&u.uz) || Is_waterlevel(&u.uz)) {
		/* it must be air -- water checked above */
		pline("You swing %s through thin air.", yobjnam(obj, NULL));
	} else if (!can_reach_floor()) {
		pline("You can't reach the %s.", surface(u.ux, u.uy));
	} else if (is_pool(u.ux, u.uy) || is_lava(u.ux, u.uy)) {
		/* Monsters which swim also happen not to be able to dig */
		pline("You cannot stay under%s long enough.",
		      is_pool(u.ux, u.uy) ? "water" : " the lava");
	} else if (digtyp == 2) {
		pline("%s merely scratches the %s.",
		      Yobjnam2(obj, NULL), surface(u.ux, u.uy));
		u_wipe_engr(3);
	} else {
		if (context.digging.pos.x != u.ux || context.digging.pos.y != u.uy ||
		    !on_level(&context.digging.level, &u.uz) || !context.digging.down) {
			context.digging.chew = false;
			context.digging.down = true;
			context.digging.warned = false;
			context.digging.pos.x = u.ux;
			context.digging.pos.y = u.uy;
			assign_level(&context.digging.level, &u.uz);
			context.digging.effort = 0;
			pline("You start %s downward.", verbing);
			if (*u.ushops) shopdig(0);
		} else
			pline("You continue %s downward.", verbing);
		did_dig_msg = false;
		set_occupation(dig, verbing, 0);
	}
	return 1;
}

/*
 * Town Watchmen frown on damage to the town walls, trees or fountains.
 * It's OK to dig holes in the ground, however.
 * If mtmp is assumed to be a watchman, a watchman is found if mtmp == 0
 * zap == true if wand/spell of digging, false otherwise (chewing)
 */
void watch_dig(struct monst *mtmp, xchar x, xchar y, boolean zap) {
	struct rm *lev = &levl[x][y];

	if (in_town(x, y) &&
	    (closed_door(x, y) || lev->typ == SDOOR ||
	     IS_WALL(lev->typ) || IS_FOUNTAIN(lev->typ) || IS_TREE(lev->typ))) {
		if (!mtmp) {
			for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
				if (DEADMONSTER(mtmp)) continue;
				if ((mtmp->data == &mons[PM_WATCHMAN] ||
				     mtmp->data == &mons[PM_WATCH_CAPTAIN]) &&
				    mtmp->mcansee && m_canseeu(mtmp) &&
				    couldsee(mtmp->mx, mtmp->my) && mtmp->mpeaceful)
					break;
			}
		}

		if (mtmp) {
			if (zap || context.digging.warned) {
				verbalize("Halt, vandal!  You're under arrest!");
				angry_guards(Deaf);
			} else {
				const char *str;

				if (IS_DOOR(lev->typ))
					str = "door";
				else if (IS_TREE(lev->typ))
					str = "tree";
				else if (IS_ROCK(lev->typ))
					str = "wall";
				else
					str = "fountain";
				verbalize("Hey, stop damaging that %s!", str);
				context.digging.warned = true;
			}
			if (is_digging())
				stop_occupation();
		}
	}
}

/* Return true if monster died, false otherwise.  Called from m_move(). */
boolean mdig_tunnel(struct monst *mtmp) {
	struct rm *here;
	int pile = rnd(12);

	here = &levl[mtmp->mx][mtmp->my];
	if (here->typ == SDOOR)
		cvt_sdoor_to_door(here); /* ->typ = DOOR */

	/* Eats away door if present & closed or locked */
	if (closed_door(mtmp->mx, mtmp->my)) {
		if (*in_rooms(mtmp->mx, mtmp->my, SHOPBASE))
			add_damage(mtmp->mx, mtmp->my, 0L);
		unblock_point(mtmp->mx, mtmp->my); /* vision */
		if (here->doormask & D_TRAPPED) {
			here->doormask = D_NODOOR;
			if (mb_trapped(mtmp)) { /* mtmp is killed */
				newsym(mtmp->mx, mtmp->my);
				return true;
			}
		} else {
			if (!rn2(3) && flags.verbose) /* not too often.. */
				pline("You feel an unexpected draft.");
			here->doormask = D_BROKEN;
		}
		newsym(mtmp->mx, mtmp->my);
		return false;
	} else if (!IS_ROCK(here->typ) && !IS_TREE(here->typ)) /* no dig */
		return false;

	/* Only rock, trees, and walls fall through to this point. */
	if ((here->wall_info & W_NONDIGGABLE) != 0) {
		impossible("mdig_tunnel:  %s at (%d,%d) is undiggable",
			   (IS_WALL(here->typ) ? "wall" : "stone"),
			   (int)mtmp->mx, (int)mtmp->my);
		return false; /* still alive */
	}

	if (IS_WALL(here->typ)) {
		/* KMH -- Okay on arboreal levels (room walls are still stone) */
		if (flags.verbose && !rn2(5))
			/* KMH -- Okay on arboreal levels (room walls are still stone) */
			You_hear("crashing rock.");
		if (*in_rooms(mtmp->mx, mtmp->my, SHOPBASE))
			add_damage(mtmp->mx, mtmp->my, 0L);
		if (level.flags.is_maze_lev) {
			here->typ = ROOM;
		} else if (level.flags.is_cavernous_lev &&
			   !in_town(mtmp->mx, mtmp->my)) {
			here->typ = CORR;
		} else {
			here->typ = DOOR;
			here->doormask = D_NODOOR;
		}
	} else if (IS_TREE(here->typ)) {
		here->typ = ROOM;
		if (pile && pile < 5)
			rnd_treefruit_at(mtmp->mx, mtmp->my);
	} else {
		here->typ = CORR;
		if (pile && pile < 5)
			mksobj_at((pile == 1) ? BOULDER : ROCK,
				  mtmp->mx, mtmp->my, true, false);
	}
	newsym(mtmp->mx, mtmp->my);
	if (!sobj_at(BOULDER, mtmp->mx, mtmp->my))
		unblock_point(mtmp->mx, mtmp->my); /* vision */

	return false;
}

/* digging via wand zap or spell cast */
void zap_dig(void) {
	struct rm *room;
	struct monst *mtmp;
	/*        struct obj *otmp;*/
	struct obj *otmp, *next_obj;
	int zx, zy, digdepth;
	boolean shopdoor, shopwall, maze_dig;
	/*
	 * Original effect (approximately):
	 * from CORR: dig until we pierce a wall
	 * from ROOM: pierce wall and dig until we reach
	 * an ACCESSIBLE place.
	 * Currently: dig for digdepth positions;
	 * also down on request of Lennart Augustsson.
	 */

	if (u.uswallow) {
		mtmp = u.ustuck;

		if (!is_whirly(mtmp->data)) {
			if (is_animal(mtmp->data))
				pline("You pierce %s %s wall!",
				      s_suffix(mon_nam(mtmp)), mbodypart(mtmp, STOMACH));
			mtmp->mhp = 1; /* almost dead */
			expels(mtmp, mtmp->data, !is_animal(mtmp->data));
		}
		return;
	} /* swallowed */

	if (u.dz) {
		if (!Is_airlevel(&u.uz) && !Is_waterlevel(&u.uz) && !Underwater) {
			if (u.dz < 0 || On_stairs(u.ux, u.uy)) {
				if (On_stairs(u.ux, u.uy))
					pline("The beam bounces off the %s and hits the %s.",
					      (u.ux == xdnladder || u.ux == xupladder) ?
						      "ladder" :
						      "stairs",
					      ceiling(u.ux, u.uy));
				pline("You loosen a rock from the %s.", ceiling(u.ux, u.uy));
				pline("It falls on your %s!", body_part(HEAD));
				losehp(Maybe_Half_Phys(rnd((uarmh && is_metallic(uarmh)) ? 2 : 6)), "falling rock", KILLED_BY_AN);
				otmp = mksobj_at(ROCK, u.ux, u.uy, false, false);
				if (otmp) {
					xname(otmp); /* set dknown, maybe bknown */
					stackobj(otmp);
				}
				newsym(u.ux, u.uy);
			} else {
				watch_dig(NULL, u.ux, u.uy, true);
				dighole(false);
			}
		}
		return;
	} /* up or down */

	/* normal case: digging across the level */
	shopdoor = shopwall = false;
	maze_dig = level.flags.is_maze_lev && !Is_earthlevel(&u.uz);
	zx = u.ux + u.dx;
	zy = u.uy + u.dy;
	digdepth = rn1(18, 8);
	tmp_at(DISP_BEAM, cmap_to_glyph(S_digbeam));
	while (--digdepth >= 0) {
		if (!isok(zx, zy)) break;
		room = &levl[zx][zy];
		tmp_at(zx, zy);
		delay_output(); /* wait a little bit */

		/* WAC check for monster, boulder */
		if ((mtmp = m_at(zx, zy)) != 0) {
			if (made_of_rock(mtmp->data)) {
				pline("You gouge a hole in %s!", mon_nam(mtmp));
				mtmp->mhp /= 2;
				if (mtmp->mhp < 1) mtmp->mhp = 1;
				setmangry(mtmp);
			} else
				pline("%s is unaffected!", Monnam(mtmp));
		}
		for (otmp = level.objects[zx][zy]; otmp; otmp = next_obj) {
			next_obj = otmp->nexthere;
			/* vaporize boulders */
			if (otmp->otyp == BOULDER) {
				delobj(otmp);
				/* A little Sokoban guilt... */
				if (In_sokoban(&u.uz))
					change_luck(-1);
				unblock_point(zx, zy);
				newsym(zx, zy);
				pline("The boulder is vaporized!");
			}
			break;
		}

		if (closed_door(zx, zy) || room->typ == SDOOR) {
			/* ALI - Artifact doors */
			if (artifact_door(zx, zy)) {
				if (cansee(zx, zy))
					pline("The door glows then fades.");
				break;
			}
			if (*in_rooms(zx, zy, SHOPBASE)) {
				add_damage(zx, zy, 400L);
				shopdoor = true;
			}
			if (room->typ == SDOOR)
				room->typ = DOOR;
			else if (cansee(zx, zy))
				pline("The door is razed!");
			watch_dig(NULL, zx, zy, true);
			room->doormask = D_NODOOR;
			unblock_point(zx, zy); /* vision */
			digdepth -= 2;
			if (maze_dig) break;
		} else if (maze_dig) {
			if (IS_WALL(room->typ)) {
				if (!(room->wall_info & W_NONDIGGABLE)) {
					if (*in_rooms(zx, zy, SHOPBASE)) {
						add_damage(zx, zy, 200L);
						shopwall = true;
					}
					room->typ = ROOM;
					unblock_point(zx, zy); /* vision */
				} else if (!Blind)
					pline("The wall glows then fades.");
				break;
			} else if (IS_TREE(room->typ)) { /* check trees before stone */
				if (!(room->wall_info & W_NONDIGGABLE)) {
					room->typ = ROOM;
					unblock_point(zx, zy); /* vision */
				} else if (!Blind)
					pline("The tree shudders but is unharmed.");
				break;
			} else if (room->typ == STONE || room->typ == SCORR) {
				if (!(room->wall_info & W_NONDIGGABLE)) {
					room->typ = CORR;
					unblock_point(zx, zy); /* vision */
				} else if (!Blind)
					pline("The rock glows then fades.");
				break;
			}
		} else if (IS_ROCK(room->typ)) {
			if (!may_dig(zx, zy)) break;
			if (IS_WALL(room->typ) || room->typ == SDOOR) {
				if (*in_rooms(zx, zy, SHOPBASE)) {
					add_damage(zx, zy, 200L);
					shopwall = true;
				}
				watch_dig(NULL, zx, zy, true);
				if (level.flags.is_cavernous_lev && !in_town(zx, zy)) {
					room->typ = CORR;
				} else {
					room->typ = DOOR;
					room->doormask = D_NODOOR;
				}
				digdepth -= 2;
			} else if (IS_TREE(room->typ)) {
				room->typ = ROOM;
				digdepth -= 2;
			} else { /* IS_ROCK but not IS_WALL or SDOOR */
				room->typ = CORR;
				digdepth--;
			}
			unblock_point(zx, zy); /* vision */
		}
		zx += u.dx;
		zy += u.dy;
	}		     /* while */
	tmp_at(DISP_END, 0); /* closing call */
	if (shopdoor || shopwall)
		pay_for_damage(shopdoor ? "destroy" : "dig into", false);
	return;
}

/* move objects from fobj/nexthere lists to buriedobjlist, keeping position */
/* information */
struct obj *bury_an_obj(struct obj *otmp) {
	struct obj *otmp2;
	boolean under_ice;

#ifdef DEBUG
	pline("bury_an_obj: %s", xname(otmp));
#endif
	if (otmp == uball)
		unpunish();
	/* after unpunish(), or might get deallocated chain */
	otmp2 = otmp->nexthere;
	/*
	 * obj_resists(,0,0) prevents Rider corpses from being buried.
	 * It also prevents The Amulet and invocation tools from being
	 * buried.  Since they can't be confined to bags and statues,
	 * it makes sense that they can't be buried either, even though
	 * the real reason there (direct accessibility when carried) is
	 * completely different.
	 */
	if (otmp == uchain || obj_resists(otmp, 0, 0))
		return otmp2;

	if (otmp->otyp == LEASH && otmp->leashmon != 0)
		o_unleash(otmp);

	if (otmp == usaddle)
		dismount_steed(DISMOUNT_GENERIC);

	if (otmp->lamplit && otmp->otyp != POT_OIL)
		end_burn(otmp, true);

	obj_extract_self(otmp);

	under_ice = is_ice(otmp->ox, otmp->oy);
	if (otmp->otyp == ROCK && !under_ice) {
		/* merges into burying material */
		obfree(otmp, NULL);
		return otmp2;
	}
	/*
	 * Start a rot on organic material.  Not corpses -- they
	 * are already handled.
	 */
	if (otmp->otyp == CORPSE) {
		; /* should cancel timer if under_ice */
	} else if ((under_ice ? otmp->oclass == POTION_CLASS : is_organic(otmp)) && !obj_resists(otmp, 5, 95)) {
		start_timer((under_ice ? 0L : 250L) + rnd(250), TIMER_OBJECT, ROT_ORGANIC, obj_to_any(otmp));
	}
	add_to_buried(otmp);
	return otmp2;
}

void bury_objs(int x, int y) {
	struct obj *otmp, *otmp2;

#ifdef DEBUG
	if (level.objects[x][y] != NULL)
		pline("bury_objs: at %d, %d", x, y);
#endif
	for (otmp = level.objects[x][y]; otmp; otmp = otmp2)
		otmp2 = bury_an_obj(otmp);

	/* don't expect any engravings here, but just in case */
	del_engr_at(x, y);
	newsym(x, y);
}

/* move objects from buriedobjlist to fobj/nexthere lists */
void unearth_objs(int x, int y) {
	struct obj *otmp, *otmp2;

#ifdef DEBUG
	pline("unearth_objs: at %d, %d", x, y);
#endif
	for (otmp = level.buriedobjlist; otmp; otmp = otmp2) {
		otmp2 = otmp->nobj;
		if (otmp->ox == x && otmp->oy == y) {
			obj_extract_self(otmp);
			if (otmp->timed)
				stop_timer(ROT_ORGANIC, obj_to_any(otmp));
			place_object(otmp, x, y);
			stackobj(otmp);
		}
	}
	del_engr_at(x, y);
	newsym(x, y);
}

/*
 * The organic material has rotted away while buried.  As an expansion,
 * we could add add partial damage.  A damage count is kept in the object
 * and every time we are called we increment the count and reschedule another
 * timeout.  Eventually the object rots away.
 *
 * This is used by buried objects other than corpses.  When a container rots
 * away, any contents become newly buried objects.
 */
/* ARGSUSED */
void rot_organic(void *arg, long timeout) {
#if defined(MAC_MPW)
#pragma unused(timeout)
#endif
	struct obj *obj = (struct obj *)arg;

	while (Has_contents(obj)) {
		/* We don't need to place contained object on the floor
		   first, but we do need to update its map coordinates. */
		obj->cobj->ox = obj->ox, obj->cobj->oy = obj->oy;
		/* Everything which can be held in a container can also be
		   buried, so bury_an_obj's use of obj_extract_self insures
		   that Has_contents(obj) will eventually become false. */
		bury_an_obj(obj->cobj);
	}
	obj_extract_self(obj);
	obfree(obj, NULL);
}

/*
 * Called when a corpse has rotted completely away.
 */
void rot_corpse(void *arg, long timeout) {
	xchar x = 0, y = 0;
	struct obj *obj = (struct obj *)arg;
	boolean on_floor = obj->where == OBJ_FLOOR,
		in_minvent = obj->where == OBJ_MINVENT,
		in_invent = obj->where == OBJ_INVENT;

	if (on_floor) {
		x = obj->ox;
		y = obj->oy;
	} else if (in_minvent) {
		/* WAC unwield if wielded */
		if (MON_WEP(obj->ocarry) && MON_WEP(obj->ocarry) == obj) {
			obj->owornmask &= ~W_WEP;
			MON_NOWEP(obj->ocarry);
		}
	} else if (in_invent) {
		if (flags.verbose) {
			char *cname = corpse_xname(obj, false);
			pline("Your %s%s %s away%c",
			      obj == uwep ? "wielded " : nul, cname,
			      otense(obj, "rot"), obj == uwep ? '!' : '.');
		}
		if (obj == uwep) {
			uwepgone(); /* now bare handed */
			stop_occupation();
		} else if (obj == uswapwep) {
			uswapwepgone();
			stop_occupation();
		} else if (obj == uquiver) {
			uqwepgone();
			stop_occupation();
		} else if (obj == uswapwep) {
			uswapwepgone();
			stop_occupation();
		} else if (obj == uquiver) {
			uqwepgone();
			stop_occupation();
		}
	} else if (obj->where == OBJ_MINVENT && obj->owornmask) {
		if (obj == MON_WEP(obj->ocarry)) {
			setmnotwielded(obj->ocarry, obj);
			MON_NOWEP(obj->ocarry);
		}
	}
	rot_organic(arg, timeout);
	if (on_floor) {
		struct monst *mtmp = m_at(x, y);

		// a hiding monster may be exposed
		if (mtmp && !OBJ_AT(x, y) &&
				mtmp->mundetected && hides_under(mtmp->data)) {
			mtmp->mundetected = 0;
		}

		newsym(x, y);
	} else if (in_invent) {
		update_inventory();
	}
}

#ifdef DEBUG

// in this case, bury everything at your loc and around
int wiz_debug_cmd(void) {
	int x, y;

	for (x = u.ux - 1; x <= u.ux + 1; x++)
		for (y = u.uy - 1; y <= u.uy + 1; y++)
			if (isok(x, y)) bury_objs(x, y);
	return 0;
}

#endif /* DEBUG */

/*dig.c*/
