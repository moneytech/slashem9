/*	SCCS Id: @(#)spell.c	3.4	2003/01/17	*/
/*	Copyright (c) M. Stephenson 1988			  */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

/* spellmenu arguments; 0 thru n-1 used as spl_book[] index when swapping */
#define SPELLMENU_CAST (-2)
#define SPELLMENU_VIEW (-1)

#define KEEN	      10000 /* memory increase reading the context.spbook.book */
#define CAST_BOOST    500   /* memory increase for successful casting */
#define MAX_KNOW      70000 /* Absolute Max timeout */
#define MAX_CAN_STUDY 60000 /* Can study while timeout is less than */

#define MAX_STUDY_TIME	300 /* Max time for one study session */
#define MAX_SPELL_STUDY 30  /* Uses before spellbook crumbles */

#define spellknow(spell) spl_book[spell].sp_know

#define incrnknow(spell)	spl_book[spell].sp_know = ((spl_book[spell].sp_know < 1) ? KEEN : ((spl_book[spell].sp_know + KEEN) > MAX_KNOW) ? MAX_KNOW : spl_book[spell].sp_know + KEEN)
#define boostknow(spell, boost) spl_book[spell].sp_know = ((spl_book[spell].sp_know + boost > MAX_KNOW) ? MAX_KNOW : spl_book[spell].sp_know + boost)

#define spellev(spell)	 spl_book[spell].sp_lev
#define spellid(spell)	 spl_book[spell].sp_id
#define spellname(spell) OBJ_NAME(objects[spellid(spell)])
#define spellet(spell)                                             \
	((char)((spell < 26) ? ('a' + spell) :                     \
			       (spell < 52) ? ('A' + spell - 26) : \
					      (spell < 62) ? ('0' + spell - 52) : 0))

static int spell_let_to_idx(char);
static boolean cursed_book(struct obj *bp);
static boolean confused_book(struct obj *);
static void deadbook(struct obj *);
static int learn(void);
static void do_reset_learn(void);
static boolean getspell(int *);
static boolean dospellmenu(const char *, int, int *);
static int percent_success(int);
static void cast_protection(void);
static void spell_backfire(int);
static const char *spelltypemnemonic(int);
static int isqrt(int);

/* The roles[] table lists the role-specific values for tuning
 * percent_success().
 *
 * Reasoning:
 *   splcaster, special:
 *	Arc are aware of magic through historical research
 *	Bar abhor magic (Conan finds it "interferes with his animal instincts")
 *	Cav are ignorant to magic
 *	Hea are very aware of healing magic through medical research
 *	Kni are moderately aware of healing from Paladin training
 *	Mon use magic to attack and defend in lieu of weapons and armor
 *	Pri are very aware of healing magic through theological research
 *	Ran avoid magic, preferring to fight unseen and unheard
 *	Rog are moderately aware of magic through trickery
 *	Sam have limited magical awareness, prefering meditation to conjuring
 *	Tou are aware of magic from all the great films they have seen
 *	Val have limited magical awareness, prefering fighting
 *	Wiz are trained mages
 *
 *	The arms penalty is lessened for trained fighters Bar, Kni, Ran,
 *	Sam, Val -
 *	the penalty is its metal interference, not encumbrance.
 *	The `spelspec' is a single spell which is fundamentally easier
 *	 for that role to cast.
 *
 *  spelspec, spelsbon:
 *	Arc map masters (SPE_MAGIC_MAPPING)
 *	Bar fugue/berserker (SPE_HASTE_SELF)
 *	Cav born to dig (SPE_DIG)
 *	Hea to heal (SPE_CURE_SICKNESS)
 *	Kni to turn back evil (SPE_TURN_UNDEAD)
 *	Mon to preserve their abilities (SPE_RESTORE_ABILITY)
 *	Pri to bless (SPE_REMOVE_CURSE)
 *	Ran to hide (SPE_INVISIBILITY)
 *	Rog to find loot (SPE_DETECT_TREASURE)
 *	Sam to be At One (SPE_CLAIRVOYANCE)
 *	Tou to smile (SPE_CHARM_MONSTER)
 *	Val control lightning (SPE_LIGHTNING)
 *	Wiz all really, but SPE_MAGIC_MISSILE is their party trick
 *	Yeo guard doors (SPE_KNOCK)
 *
 *	See percent_success() below for more comments.
 *
 *  uarmbon, uarmsbon, uarmhbon, uarmgbon, uarmfbon:
 *	Fighters find body armour & shield a little less limiting.
 *	Headgear, Gauntlets and Footwear are not role-specific (but
 *	still have an effect, except helm of brilliance, which is designed
 *	to permit magic-use).
 */

#define uarmhbon 4 /* Metal helmets interfere with the mind */
#define uarmgbon 6 /* Casting channels through the hands */
#define uarmfbon 2 /* All metal interferes to some degree */

/* since the spellbook itself doesn't blow up, don't say just "explodes" */
static const char explodes[] = "radiates explosive energy";

/* convert an alnum into a number in the range 0..61, or -1 if not an alnum */
static int spell_let_to_idx(char ilet) {
	int indx;

	indx = ilet - 'a';
	if (indx >= 0 && indx < 26) return indx;
	indx = ilet - 'A';
	if (indx >= 0 && indx < 26) return indx + 26;
	indx = ilet - '0';
	if (indx >= 0 && indx < 10) return indx + 52;
	return -1;
}

/* true: context.spbook.book should be destroyed by caller */
static boolean cursed_book(struct obj *bp) {
	int lev = objects[bp->otyp].oc_level;

	switch (rn2(lev)) {
		case 0:
			pline("You feel a wrenching sensation.");
			tele(); /* teleport him */
			break;
		case 1:
			pline("You feel threatened.");
			aggravate();
			break;
		case 2:
			/* [Tom] lowered this (used to be 100,250) */
			make_blinded(Blinded + rn1(50, 25), true);
			break;
		case 3:
			take_gold();
			break;
		case 4:
			pline("These runes were just too much to comprehend.");
			make_confused(HConfusion + rn1(7, 16), false);
			break;
		case 5:
			pline("The book was coated with contact poison!");
			if (uarmg) {
				if (uarmg->oerodeproof || !is_corrodeable(uarmg)) {
					pline("Your gloves seem unaffected.");
				} else if (uarmg->oeroded2 < MAX_ERODE) {
					if (uarmg->greased) {
						grease_protect(uarmg, "gloves", &youmonst);
					} else {
						pline("Your gloves corrode%s!",
						      uarmg->oeroded2 + 1 == MAX_ERODE ?
							      " completely" :
							      uarmg->oeroded2 ?
							      " further" :
							      "");
						uarmg->oeroded2++;
					}
				} else
					pline("Your gloves %s completely corroded.",
					      Blind ? "feel" : "look");
				break;
			}
			/* temp disable in_use; death should not destroy the context.spbook.book */
			bp->in_use = false;
			losestr(Poison_resistance ? rn1(2, 1) : rn1(4, 3));
			losehp(rnd(Poison_resistance ? 6 : 10),
			       "contact-poisoned spellbook", KILLED_BY_AN);
			bp->in_use = true;
			break;
		case 6:
			if (Antimagic) {
				shieldeff(u.ux, u.uy);
				pline("The book %s, but you are unharmed!", explodes);
			} else {
				pline("As you read the book, it %s in your %s!",
				      explodes, body_part(FACE));
				losehp(Maybe_Half_Phys(2 * rnd(10) + 5), "exploding rune", KILLED_BY_AN);
			}
			return true;
		default:
			rndcurse();
			break;
	}
	return false;
}

/* study while confused: returns true if the context.spbook.book is destroyed */
static boolean confused_book(struct obj *spellbook) {
	boolean gone = false;

	if (!rn2(3) && spellbook->otyp != SPE_BOOK_OF_THE_DEAD) {
		spellbook->in_use = true; /* in case called from learn */
		pline(
			"Being confused you have difficulties in controlling your actions.");
		display_nhwindow(WIN_MESSAGE, false);
		pline("You accidentally tear the spellbook to pieces.");
		if (!objects[spellbook->otyp].oc_name_known &&
		    !objects[spellbook->otyp].oc_uname)
			docall(spellbook);
		if (carried(spellbook))
			useup(spellbook);
		else
			useupf(spellbook, 1L);
		gone = true;
	} else {
		pline("You find yourself reading the %s line over and over again.",
		      spellbook == context.spbook.book ? "next" : "first");
	}
	return gone;
}

/* special effects for The Book of the Dead */
static void deadbook(struct obj *book2) {
	struct monst *mtmp, *mtmp2;
	coord mm;

	pline("You turn the pages of the Book of the Dead...");
	makeknown(SPE_BOOK_OF_THE_DEAD);
	/* KMH -- Need ->known to avoid "_a_ Book of the Dead" */
	book2->known = 1;
	if (invocation_pos(u.ux, u.uy) && !On_stairs(u.ux, u.uy)) {
		struct obj *otmp;
		boolean arti1_primed = false, arti2_primed = false,
			arti_cursed = false;

		if (book2->cursed) {
			pline("The runes appear scrambled.  You can't read them!");
			return;
		}

		if (!u.uhave.bell || !u.uhave.menorah) {
			pline("A chill runs down your %s.", body_part(SPINE));
			if (!u.uhave.bell) You_hear("a faint chime...");
			if (!u.uhave.menorah) pline("Vlad's doppelganger is amused.");
			return;
		}

		for (otmp = invent; otmp; otmp = otmp->nobj) {
			if (otmp->otyp == CANDELABRUM_OF_INVOCATION &&
			    otmp->spe == 7 && otmp->lamplit) {
				if (!otmp->cursed)
					arti1_primed = true;
				else
					arti_cursed = true;
			}
			if (otmp->otyp == BELL_OF_OPENING &&
			    (moves - otmp->age) < 5L) { /* you rang it recently */
				if (!otmp->cursed)
					arti2_primed = true;
				else
					arti_cursed = true;
			}
		}

		if (arti_cursed) {
			pline("The invocation fails!");
			pline("At least one of your artifacts is cursed...");
		} else if (arti1_primed && arti2_primed) {
			unsigned soon = (unsigned)d(2, 6); /* time til next intervene() */

			/* successful invocation */
			mkinvokearea();
			u.uevent.invoked = 1;
			/* in case you haven't killed the Wizard yet, behave as if
			   you just did */
			u.uevent.udemigod = 1; /* wizdead() */
			if (!u.udg_cnt || u.udg_cnt > soon) u.udg_cnt = soon;
		} else { /* at least one artifact not prepared properly */
			pline("You have a feeling that something is amiss...");
			goto raise_dead;
		}
		return;
	}

	/* when not an invocation situation */
	if (book2->cursed) {
	raise_dead:

		pline("You raised the dead!");
		/* first maybe place a dangerous adversary */
		if (!rn2(3) && ((mtmp = makemon(&mons[PM_MASTER_LICH],
						u.ux, u.uy, NO_MINVENT)) != 0 ||
				(mtmp = makemon(&mons[PM_NALFESHNEE],
						u.ux, u.uy, NO_MINVENT)) != 0)) {
			mtmp->mpeaceful = 0;
			set_malign(mtmp);
		}
		/* next handle the affect on things you're carrying */
		unturn_dead(&youmonst);
		/* last place some monsters around you */
		mm.x = u.ux;
		mm.y = u.uy;
		mkundead(&mm, true, NO_MINVENT);
	} else if (book2->blessed) {
		for (mtmp = fmon; mtmp; mtmp = mtmp2) {
			mtmp2 = mtmp->nmon; /* tamedog() changes chain */
			if (DEADMONSTER(mtmp)) continue;

			if ((is_undead(mtmp->data) || is_vampshifter(mtmp)) && cansee(mtmp->mx, mtmp->my)) {
				mtmp->mpeaceful = true;
				if (sgn(mtmp->data->maligntyp) == sgn(u.ualign.type) && distu(mtmp->mx, mtmp->my) < 4)
					if (mtmp->mtame) {
						if (mtmp->mtame < 20)
							mtmp->mtame++;
					} else
						tamedog(mtmp, NULL);
				else
					monflee(mtmp, 0, false, true);
			}
		}
	} else {
		switch (rn2(3)) {
			case 0:
				pline("Your ancestors are annoyed with you!");
				break;
			case 1:
				pline("The headstones in the cemetery begin to move!");
				break;
			default:
				pline("Oh my!  Your name appears in the book!");
		}
	}
	return;
}

static int learn() {
	int i;
	short booktype;
	char splname[BUFSZ];
	boolean costly = true;

	if (!context.spbook.book || !(carried(context.spbook.book) ||
		       (context.spbook.book->where == OBJ_FLOOR &&
			context.spbook.book->ox == u.ux && context.spbook.book->oy == u.uy))) {
		/* maybe it was stolen or polymorphed? */
		do_reset_learn();
		return 0;
	}
	/* JDS: lenses give 50% faster reading; 33% smaller read time */
	if (context.spbook.delay < context.spbook.end_delay && ublindf && ublindf->otyp == LENSES && rn2(2))
		context.spbook.delay++;
	if (Confusion) { /* became confused while learning */
		confused_book(context.spbook.book);
		context.spbook.book = NULL;		  /* no longer studying */
		context.spbook.o_id = 0;
		nomul(context.spbook.delay - context.spbook.end_delay); /* remaining context.spbook.delay is uninterrupted */
		context.spbook.delay = context.spbook.end_delay;
		return 0;
	}
	if (context.spbook.delay < context.spbook.end_delay) { /* not if (context.spbook.delay++), so at end context.spbook.delay == 0 */
		context.spbook.delay++;
		return 1; /* still busy */
	}
	exercise(A_WIS, true); /* you're studying. */
	booktype = context.spbook.book->otyp;
	if (booktype == SPE_BOOK_OF_THE_DEAD) {
		deadbook(context.spbook.book);
		return 0;
	}

	sprintf(splname, objects[booktype].oc_name_known ? "\"%s\"" : "the \"%s\" spell",
		OBJ_NAME(objects[booktype]));
	for (i = 0; i < MAXSPELL; i++) {
		if (spellid(i) == booktype) {
			if (context.spbook.book->spestudied > MAX_SPELL_STUDY) {
				pline("This spellbook is too faint to be read anymore.");
				context.spbook.book->otyp = booktype = SPE_BLANK_PAPER;
			} else if (spellknow(i) <= MAX_CAN_STUDY) {
				pline("Your knowledge of that spell is keener.");
				incrnknow(i);
				context.spbook.book->spestudied++;
				if (context.spbook.end_delay) {
					boostknow(i,
						  context.spbook.end_delay * (context.spbook.book->spe > 0 ? 20 : 10));
					use_skill(spell_skilltype(context.spbook.book->otyp),
						  context.spbook.end_delay / (context.spbook.book->spe > 0 ? 10 : 20));
				}
				exercise(A_WIS, true); /* extra study */
			} else {		       /* MAX_CAN_STUDY < spellknow(i) <= MAX_SPELL_STUDY */
				pline("You know %s quite well already.", splname);
				costly = false;
			}
			makeknown((int)booktype);
			break;
		} else if (spellid(i) == NO_SPELL) {
			spl_book[i].sp_id = booktype;
			spl_book[i].sp_lev = objects[booktype].oc_level;
			incrnknow(i);
			context.spbook.book->spestudied++;
			pline("You have keen knowledge of the spell.");
			pline(i > 0 ? "You add %s to your repertoire." : "You learn %s.", splname);
			makeknown((int)booktype);
			break;
		}
	}
	if (i == MAXSPELL) impossible("Too many spells memorized!");

	if (context.spbook.book->cursed) { /* maybe a demon cursed it */
		if (cursed_book(context.spbook.book)) {
			if (carried(context.spbook.book))
				useup(context.spbook.book);
			else
				useupf(context.spbook.book, 1L);
			context.spbook.book = NULL;
			context.spbook.o_id = 0;
			return 0;
		}
	}
	if (costly) check_unpaid(context.spbook.book);
	context.spbook.book = NULL;
	context.spbook.o_id = 0;
	return 0;
}

int study_book(struct obj *spellbook) {
	int booktype = spellbook->otyp;
	boolean confused = (Confusion != 0);
	boolean too_hard = false;

	if (context.spbook.delay && !confused && spellbook == context.spbook.book &&
	    /* handle the sequence: start reading, get interrupted,
	                   have context.spbook.book become erased somehow, resume reading it */
	    booktype != SPE_BLANK_PAPER) {
		pline("You continue your efforts to memorize the spell.");
	} else {
		/* KMH -- Simplified this code */
		if (booktype == SPE_BLANK_PAPER) {
			pline("This spellbook is all blank.");
			makeknown(booktype);
			return 1;
		}
		if (spellbook->spe && confused) {
			check_unpaid_usage(spellbook, true);
			consume_obj_charge(spellbook, false);
			pline("The words on the page seem to glow faintly purple.");
			pline("You can't quite make them out.");
			return 1;
		}

		switch (objects[booktype].oc_level) {
			case 1:
			case 2:
				context.spbook.delay = -objects[booktype].oc_delay;
				break;
			case 3:
			case 4:
				context.spbook.delay = -(objects[booktype].oc_level - 1) *
					objects[booktype].oc_delay;
				break;
			case 5:
			case 6:
				context.spbook.delay = -objects[booktype].oc_level *
					objects[booktype].oc_delay;
				break;
			case 7:
				context.spbook.delay = -8 * objects[booktype].oc_delay;
				break;
			default:
				impossible("Unknown spellbook level %d, book %d;",
					   objects[booktype].oc_level, booktype);
				return 0;
		}

		/* Books are often wiser than their readers (Rus.) */
		spellbook->in_use = true;
		if (!spellbook->blessed &&
		    spellbook->otyp != SPE_BOOK_OF_THE_DEAD) {
			if (spellbook->cursed) {
				too_hard = true;
			} else {
				/* uncursed - chance to fail */
				int read_ability = ACURR(A_INT) + 4 + u.ulevel / 2 - 2 * objects[booktype].oc_level + ((ublindf && ublindf->otyp == LENSES) ? 2 : 0);
				/* only wizards know if a spell is too difficult */
				if (Role_if(PM_WIZARD) && read_ability < 20 &&
				    !confused && !spellbook->spe) {
					char qbuf[QBUFSZ];
					sprintf(qbuf,
						"This spellbook is %sdifficult to comprehend. Continue?",
						(read_ability < 12 ? "very " : ""));
					if (yn(qbuf) != 'y') {
						spellbook->in_use = false;
						return 1;
					}
				}
				/* its up to random luck now */
				if (rnd(20) > read_ability) {
					too_hard = true;
				}
			}
		}

		if (too_hard && (spellbook->cursed || !spellbook->spe)) {
			boolean gone = cursed_book(spellbook);

			nomul(context.spbook.delay); /* study time */
			context.spbook.delay = 0;
			if (gone || !rn2(3)) {
				if (!gone) pline("The spellbook crumbles to dust!");
				if (!objects[spellbook->otyp].oc_name_known &&
				    !objects[spellbook->otyp].oc_uname)
					docall(spellbook);
				if (carried(spellbook))
					useup(spellbook);
				else
					useupf(spellbook, 1L);
			} else
				spellbook->in_use = false;
			return 1;
		} else if (confused) {
			if (!confused_book(spellbook)) {
				spellbook->in_use = false;
			}
			nomul(context.spbook.delay);
			context.spbook.delay = 0;
			return 1;
		}
		spellbook->in_use = false;

		/* The glowing words make studying easier */
		if (spellbook->otyp != SPE_BOOK_OF_THE_DEAD) {
			context.spbook.delay *= 2;
			if (spellbook->spe) {
				check_unpaid_usage(spellbook, true);
				consume_obj_charge(spellbook, false);
				pline("The words on the page seem to glow faintly.");
				if (!too_hard)
					context.spbook.delay /= 3;
			}
		}
		context.spbook.end_delay = 0; /* Changed if multi != 0 */

#ifdef DEBUG
		pline("Delay: %i", context.spbook.delay);
#endif
		if (multi) {
			/* Count == practice reading :) */
			char qbuf[QBUFSZ];

			if (multi + 1 > MAX_STUDY_TIME) multi = MAX_STUDY_TIME - 1;
			sprintf(qbuf, "Study for at least %i turns?", (multi + 1));
			if (ynq(qbuf) != 'y') {
				multi = 0;
				return 1;
			}
			if ((--multi) > (-context.spbook.delay)) context.spbook.end_delay = multi + context.spbook.delay;
			multi = 0;
#ifdef DEBUG
			pline("end_delay: %i", context.spbook.end_delay);
#endif
		}

		pline("You begin to %s the runes.",
		      spellbook->otyp == SPE_BOOK_OF_THE_DEAD ? "recite" :
								"memorize");
	}

	context.spbook.book = spellbook;
	if (context.spbook.book)
		context.spbook.o_id = context.spbook.book->o_id;

	set_occupation(learn, "studying", 0);
	return 1;
}

/* a spellbook has been destroyed or the character has changed levels;
   the stored address for the current context.spbook.book is no longer valid */
void book_disappears(struct obj *obj) {
	if (obj == context.spbook.book) {
		context.spbook.book = NULL;
		context.spbook.o_id = 0;
	}
}

/* renaming an object usually results in it having a different address;
   so the sequence start reading, get interrupted, name the context.spbook.book, resume
   reading would read the "new" context.spbook.book from scratch */
void book_substitution(struct obj *old_obj, struct obj *new_obj) {
	if (old_obj == context.spbook.book) {
		context.spbook.book = new_obj;
		if (context.spbook.book)
			context.spbook.o_id = context.spbook.book->o_id;
	}
}

static void do_reset_learn(void) {
	stop_occupation();
}

/* called from moveloop() */
void age_spells(void) {
	int i;
	/*
	 * The time relative to the hero (a pass through move
	 * loop) causes all spell knowledge to be decremented.
	 * The hero's speed, rest status, conscious status etc.
	 * does not alter the loss of memory.
	 */
	for (i = 0; i < MAXSPELL && spellid(i) != NO_SPELL; i++)
		if (spellknow(i))
			decrnknow(i);
	return;
}

/*
 * Return true if a spell was picked, with the spell index in the return
 * parameter.  Otherwise return false.
 */
static boolean getspell(int *spell_no) {
	int nspells, idx;
	char ilet, lets[QBUFSZ], qbuf[BUFSZ];

	if (spellid(0) == NO_SPELL) {
		pline("You don't know any spells right now.");
		return false;
	}
	if (flags.menu_style == MENU_TRADITIONAL) {
		/* we know there is at least 1 known spell */
		for (nspells = 1; nspells < MAXSPELL && spellid(nspells) != NO_SPELL; nspells++)
			continue;

		if (nspells == 1)
			strcpy(lets, "a");
		else if (nspells < 27)
			sprintf(lets, "a-%c", 'a' + nspells - 1);
		else if (nspells == 27)
			sprintf(lets, "a-z A");
		else if (nspells < 53)
			sprintf(lets, "a-z A-%c", 'A' + nspells - 27);
		else if (nspells == 53)
			sprintf(lets, "a-z A-Z 0");
		else if (nspells < 62)
			sprintf(lets, "a-z A-Z 0-%c", '0' + nspells - 53);
		else
			sprintf(lets, "a-z A-Z 0-9");

		for (;;) {
			sprintf(qbuf, "Cast which spell? [%s ?]", lets);
			if ((ilet = yn_function(qbuf, NULL, '\0')) == '?')
				break;

			if (index(quitchars, ilet))
				return false;

			idx = spell_let_to_idx(ilet);
			if (idx >= 0 && idx < nspells) {
				*spell_no = idx;
				return true;
			} else
				pline("You don't know that spell.");
		}
	}
	return dospellmenu("Choose which spell to cast",
			   SPELLMENU_CAST, spell_no);
}

/* the 'Z' command -- cast a spell */
int docast(void) {
	int spell_no;

	if (getspell(&spell_no))
		return spelleffects(spell_no, false);
	return 0;
}

static const char *spelltypemnemonic(int skill) {
	switch (skill) {
		case P_ATTACK_SPELL:
			return " attack";
		case P_HEALING_SPELL:
			return "healing";
		case P_DIVINATION_SPELL:
			return " divine";
		case P_ENCHANTMENT_SPELL:
			return "enchant";
		case P_PROTECTION_SPELL:
			return "protect";
		case P_BODY_SPELL:
			return "   body";
		case P_MATTER_SPELL:
			return " matter";
		default:
			impossible("Unknown spell skill, %d;", skill);
			return "";
	}
}

int spell_skilltype(int booktype) {
	return objects[booktype].oc_skill;
}

static void cast_protection() {
	int loglev = 0;
	int l = u.ulevel;
	int natac = u.uac - u.uspellprot;
	int gain;

	/* loglev=log2(u.ulevel)+1 (1..5) */
	while (l) {
		loglev++;
		l /= 2;
	}

	/* The more u.uspellprot you already have, the less you get,
	 * and the better your natural ac, the less you get.
	 *
	 *	LEVEL AC    SPELLPROT from sucessive SPE_PROTECTION casts
	 *      1     10    0,  1,  2,  3,  4
	 *      1      0    0,  1,  2,  3
	 *      1    -10    0,  1,  2
	 *      2-3   10    0,  2,  4,  5,  6,  7,  8
	 *      2-3    0    0,  2,  4,  5,  6
	 *      2-3  -10    0,  2,  3,  4
	 *      4-7   10    0,  3,  6,  8,  9, 10, 11, 12
	 *      4-7    0    0,  3,  5,  7,  8,  9
	 *      4-7  -10    0,  3,  5,  6
	 *      7-15 -10    0,  3,  5,  6
	 *      8-15  10    0,  4,  7, 10, 12, 13, 14, 15, 16
	 *      8-15   0    0,  4,  7,  9, 10, 11, 12
	 *      8-15 -10    0,  4,  6,  7,  8
	 *     16-30  10    0,  5,  9, 12, 14, 16, 17, 18, 19, 20
	 *     16-30   0    0,  5,  9, 11, 13, 14, 15
	 *     16-30 -10    0,  5,  8,  9, 10
	 */
	gain = loglev - (int)u.uspellprot / (4 - min(3, (10 - natac) / 10));

	if (gain > 0) {
		if (!Blind) {
			const char *hgolden = hcolor(NH_GOLDEN);

			if (u.uspellprot)
				pline("The %s haze around you becomes more dense.",
				      hgolden);
			else
				pline("The %s around you begins to shimmer with %s haze.",
				      /*[ what about being inside solid rock while polyd? ]*/
				      (Underwater || Is_waterlevel(&u.uz)) ? "water" : "air",
				      an(hgolden));
		}
		u.uspellprot += gain;
		u.uspmtime =
			P_SKILL(spell_skilltype(SPE_PROTECTION)) == P_EXPERT ? 20 : 10;
		if (!u.usptime)
			u.usptime = u.uspmtime;
		find_ac();
	} else {
		pline("Your skin feels warm for a moment.");
	}
}

/* attempting to cast a forgotten spell will cause disorientation */
static void spell_backfire(int spell) {
	long duration = (long)((spellev(spell) + 1) * 3); /* 6..24 */

	/* prior to 3.4.1, the only effect was confusion; it still predominates */
	switch (rn2(10)) {
		case 0:
		case 1:
		case 2:
		case 3:
			make_confused(duration, false); /* 40% */
			break;
		case 4:
		case 5:
		case 6:
			make_confused(2L * duration / 3L, false); /* 30% */
			make_stunned(duration / 3L, false);
			break;
		case 7:
		case 8:
			make_stunned(2L * duration / 3L, false); /* 20% */
			make_confused(duration / 3L, false);
			break;
		case 9:
			make_stunned(duration, false); /* 10% */
			break;
	}
	return;
}

int spelleffects(int spell, boolean atme) {
	int energy, damage, chance, n, intell;
	int hungr;
	int skill, role_skill;
	bool confused = (Confusion != 0);
	struct obj *pseudo;

	/*
	 * Find the skill the hero has in a spell type category.
	 * See spell_skilltype for categories.
	 */
	skill = spell_skilltype(spellid(spell));
	role_skill = P_SKILL(skill);

	/*
	 * Spell casting no longer affects knowledge of the spell. A
	 * decrement of spell knowledge is done every turn.
	 */
	if (spellknow(spell) <= 0) {
		pline("Your knowledge of this spell is twisted.");
		pline("It invokes nightmarish images in your mind...");
		spell_backfire(spell);
		return 0;
	} else if (spellknow(spell) <= 100) {
		pline("You strain to recall the spell.");
	} else if (spellknow(spell) <= 1000) {
		pline("Your knowledge of this spell is growing faint.");
	}
	energy = (spellev(spell) * 5); /* 5 <= energy <= 35 */

	if (u.uhunger <= 10 && spellid(spell) != SPE_DETECT_FOOD) {
		pline("You are too hungry to cast that spell.");
		return 0;
	} else if (ACURR(A_STR) < 4) {
		pline("You lack the strength to cast spells.");
		return 0;
	} else if (check_capacity(
			   "Your concentration falters while carrying so much stuff.")) {
		return 1;
	} else if (!freehand()) {
		pline("Your arms are not free to cast!");
		return 0;
	}

	if (u.uhave.amulet) {
		pline("You feel the amulet draining your energy away.");
		energy += rnd(2 * energy);
	}
	if (spellid(spell) != SPE_DETECT_FOOD) {
		hungr = energy * 2;

		/* If hero is a wizard, their current intelligence
		 * (bonuses + temporary + current)
		 * affects hunger reduction in casting a spell.
		 * 1. int = 17-18 no reduction
		 * 2. int = 16    1/4 hungr
		 * 3. int = 15    1/2 hungr
		 * 4. int = 1-14  normal reduction
		 * The reason for this is:
		 * a) Intelligence affects the amount of exertion
		 * in thinking.
		 * b) Wizards have spent their life at magic and
		 * understand quite well how to cast spells.
		 */
		intell = acurr(A_INT);
		if (!Role_if(PM_WIZARD)) intell = 10;
		switch (intell) {
			case 25:
			case 24:
			case 23:
			case 22:
			case 21:
			case 20:
			case 19:
			case 18:
			case 17:
				hungr = 0;
				break;
			case 16:
				hungr /= 4;
				break;
			case 15:
				hungr /= 2;
				break;
		}
	} else
		hungr = 0;
	/* don't put player (quite) into fainting from
	 * casting a spell, particularly since they might
	 * not even be hungry at the beginning; however,
	 * this is low enough that they must eat before
	 * casting anything else except detect food
	 */
	if (hungr > u.uhunger - 3)
		hungr = u.uhunger - 3;
	if (energy > u.uen) {
		pline("You don't have enough energy to cast that spell.");
		/* WAC/ALI Experts can override with HP/hunger loss */
		if ((role_skill >= P_SKILLED) && (yn("Continue?") == 'y')) {
			energy -= u.uen;
			hungr += energy * 2;
			if (hungr > u.uhunger - 1)
				hungr = u.uhunger - 1;
			losehp(energy, "spellcasting exhaustion", KILLED_BY);
			if (role_skill < P_EXPERT) exercise(A_WIS, false);
			energy = u.uen;
		} else
			return 0;
	}
	morehungry(hungr);

	chance = percent_success(spell);
	if (confused || (rnd(100) > chance)) {
		pline("You fail to cast the spell correctly.");

		u.uen -= (energy / 2);
		context.botl = 1;
		return 1;
	}

	u.uen -= energy;

	context.botl = 1;
	exercise(A_WIS, true);

	/* pseudo is a temporary "false" object containing the spell stats. */
	pseudo = mksobj(spellid(spell), false, false);
	pseudo->blessed = pseudo->cursed = 0;
	pseudo->quan = 20L; /* do not let useup get it */

	/* WAC -- If skilled enough,  will act like a blessed version */
	if (role_skill >= P_SKILLED)
		pseudo->blessed = 1;

	bool physical_damage = (pseudo->otyp == SPE_FORCE_BOLT);

	switch (pseudo->otyp) {
	/*
	 * At first spells act as expected.  As the hero increases in skill
	 * with the appropriate spell type, some spells increase in their
	 * effects, e.g. more damage, further distance, and so on, without
	 * additional cost to the spellcaster.
	 */
		case SPE_MAGIC_MISSILE:
		case SPE_FIREBALL:
		case SPE_CONE_OF_COLD:
		case SPE_LIGHTNING:
		case SPE_ACID_STREAM:
		case SPE_POISON_BLAST:
			if (tech_inuse(T_SIGIL_TEMPEST)) {
				weffects(pseudo);
				break;
			} /* else fall through... */
		/* these spells are all duplicates of wand effects */
		case SPE_FORCE_BOLT:
		case SPE_SLEEP:
		case SPE_KNOCK:
		case SPE_SLOW_MONSTER:
		case SPE_WIZARD_LOCK:
		case SPE_DIG:
		case SPE_TURN_UNDEAD:
		case SPE_POLYMORPH:
		case SPE_TELEPORT_AWAY:
		case SPE_CANCELLATION:
		case SPE_FINGER_OF_DEATH:
		case SPE_LIGHT:
		case SPE_DETECT_UNSEEN:
		case SPE_HEALING:
		case SPE_EXTRA_HEALING:
		case SPE_DRAIN_LIFE:
		case SPE_STONE_TO_FLESH:
			if (!(objects[pseudo->otyp].oc_dir == NODIR)) {
				if (atme)
					u.dx = u.dy = u.dz = 0;
				else if (!getdir(NULL)) {
					/* getdir cancelled, re-use previous direction */
					pline("The magical energy is released!");
				}
				if (!u.dx && !u.dy && !u.dz) {
					if ((damage = zapyourself(pseudo, true)) != 0) {
						char buf[BUFSZ];
						sprintf(buf, "zapped %sself with a spell", uhim());
						if (physical_damage) damage = Maybe_Half_Phys(damage);
						losehp(damage, buf, NO_KILLER_PREFIX);
					}
				} else {
					weffects(pseudo);
				}
			} else
				weffects(pseudo);
			update_inventory(); /* spell may modify inventory */
			break;
		/* these are all duplicates of scroll effects */
		case SPE_REMOVE_CURSE:
		case SPE_CONFUSE_MONSTER:
		case SPE_DETECT_FOOD:
		case SPE_CAUSE_FEAR:
#if 0
		/* high skill yields effect equivalent to blessed scroll */
		if (role_skill >= P_SKILLED) pseudo->blessed = 1;
#endif
		/* fall through */
		case SPE_CHARM_MONSTER:
		case SPE_MAGIC_MAPPING:
		case SPE_CREATE_MONSTER:
		case SPE_IDENTIFY:
		case SPE_COMMAND_UNDEAD:
		case SPE_SUMMON_UNDEAD:
			seffects(pseudo);
			break;

		case SPE_ENCHANT_WEAPON:
		case SPE_ENCHANT_ARMOR:
			if (role_skill >= P_EXPERT)
				n = 8;
			else if (role_skill >= P_SKILLED)
				n = 10;
			else if (role_skill >= P_BASIC)
				n = 12;
			else
				n = 14; /* Unskilled or restricted */
			if (!rn2(n)) {
				pseudo->blessed = 0;
				seffects(pseudo);
			} else
				pline("Your enchantment failed!");
			break;

		/* these are all duplicates of potion effects */
		case SPE_HASTE_SELF:
		case SPE_DETECT_TREASURE:
		case SPE_DETECT_MONSTERS:
		case SPE_LEVITATION:
		case SPE_RESTORE_ABILITY:
#if 0
		/* high skill yields effect equivalent to blessed potion */
		if (role_skill >= P_SKILLED) pseudo->blessed = 1;
#endif
		/* fall through */
		case SPE_INVISIBILITY:
			peffects(pseudo);
			break;
		case SPE_CURE_BLINDNESS:
			healup(0, 0, false, true);
			break;
		case SPE_CURE_SICKNESS:
			if (Sick) pline("You are no longer ill.");
			if (Slimed) make_slimed(0, "The slime disappears!");
			healup(0, 0, true, false);
			break;
		case SPE_CREATE_FAMILIAR:
			make_familiar(NULL, u.ux, u.uy, false);
			break;
		case SPE_CLAIRVOYANCE:
			if (!BClairvoyant)
				do_vicinity_map();
			/* at present, only one thing blocks clairvoyance */
			else if (uarmh && uarmh->otyp == CORNUTHAUM)
				pline("You sense a pointy hat on top of your %s.",
				      body_part(HEAD));
			break;
		case SPE_PROTECTION:
			cast_protection();
			break;
		case SPE_JUMPING:
			if (!jump(max(role_skill, 1)))
				pline("Nothing happens.");
			break;
		case SPE_RESIST_POISON:
			if (!(HPoison_resistance & INTRINSIC)) {
				pline("You feel healthy ..... for the moment at least.");
				incr_itimeout(&HPoison_resistance, rn1(1000, 500) +
									   spell_damage_bonus(spellid(spell)) * 100);
			} else
				pline("Nothing happens."); /* Already have as intrinsic */
			break;
		case SPE_RESIST_SLEEP:
			if (!(HSleep_resistance & INTRINSIC)) {
				if (Hallucination)
					pline("Too much coffee!");
				else
					pline("You no longer feel tired.");
				incr_itimeout(&HSleep_resistance, rn1(1000, 500) +
									  spell_damage_bonus(spellid(spell)) * 100);
			} else
				pline("Nothing happens."); /* Already have as intrinsic */
			break;
		case SPE_ENDURE_COLD:
			if (!(HCold_resistance & INTRINSIC)) {
				pline("You feel warmer.");
				incr_itimeout(&HCold_resistance, rn1(1000, 500) +
									 spell_damage_bonus(spellid(spell)) * 100);
			} else
				pline("Nothing happens."); /* Already have as intrinsic */
			break;
		case SPE_ENDURE_HEAT:
			if (!(HFire_resistance & INTRINSIC)) {
				if (Hallucination)
					pline("Excellent! You feel, like, totally cool!");
				else
					pline("You feel colder.");
				incr_itimeout(&HFire_resistance, rn1(1000, 500) +
									 spell_damage_bonus(spellid(spell)) * 100);
			} else
				pline("Nothing happens."); /* Already have as intrinsic */
			break;
		case SPE_INSULATE:
			if (!(HShock_resistance & INTRINSIC)) {
				if (Hallucination)
					pline("Bummer! You've been grounded!");
				else
					pline("You are not at all shocked by this feeling.");
				incr_itimeout(&HShock_resistance, rn1(1000, 500) +
									  spell_damage_bonus(spellid(spell)) * 100);
			} else
				pline("Nothing happens."); /* Already have as intrinsic */
			break;
		case SPE_ENLIGHTEN:
			pline("You feel self-knowledgeable...");
			display_nhwindow(WIN_MESSAGE, false);
			enlightenment(false);
			pline("The feeling subsides.");
			exercise(A_WIS, true);
			break;

		/* WAC -- new spells */
		case SPE_FLAME_SPHERE:
		case SPE_FREEZE_SPHERE: {
			int cnt = 1;
			struct monst *mtmp;

			if (role_skill >= P_SKILLED) cnt += (role_skill - P_BASIC);
			while (cnt--) {
				mtmp = make_helper((pseudo->otyp == SPE_FLAME_SPHERE) ?
							   PM_FLAMING_SPHERE :
							   PM_FREEZING_SPHERE,
						   u.ux, u.uy);
				if (!mtmp) continue;
				mtmp->mtame = 10;
				mtmp->mhpmax = mtmp->mhp = 1;
				mtmp->isspell = mtmp->uexp = true;
			} /* end while... */
			break;
		}

		/* KMH -- new spells */
		case SPE_PASSWALL:
			if (!Passes_walls)
				pline("You feel ethereal.");
			incr_itimeout(&HPasses_walls, rn1(100, 50));
			break;

		default:
			impossible("Unknown spell %d attempted.", spell);
			obfree(pseudo, NULL);
			return 0;
	}

	/* gain skill for successful cast */
	use_skill(skill, spellev(spell));

	/* WAC successful casting increases solidity of knowledge */
	boostknow(spell, CAST_BOOST);

	obfree(pseudo, NULL); /* now, get rid of it */
	return 1;
}

void losespells(void) {
	bool confused = (Confusion != 0);
	int n, nzap, i;

	context.spbook.book = NULL;
	context.spbook.o_id = 0;
	for (n = 0; n < MAXSPELL && spellid(n) != NO_SPELL; n++)
		continue;
	if (n) {
		nzap = rnd(n) + (confused ? 1 : 0);
		if (nzap > n) nzap = n;
		for (i = n - nzap; i < n; i++) {
			spellid(i) = NO_SPELL;
			exercise(A_WIS, false); /* ouch! */
		}
	}
}

/* the '+' command -- view known spells */
int dovspell(void) {
	char qbuf[QBUFSZ];
	int splnum, othnum;
	struct spell spl_tmp;

	if (spellid(0) == NO_SPELL)
		pline("You don't know any spells right now.");
	else {
		while (dospellmenu("Currently known spells",
				   SPELLMENU_VIEW, &splnum)) {
			sprintf(qbuf, "Reordering spells; swap '%s' with",
				spellname(splnum));
			if (!dospellmenu(qbuf, splnum, &othnum)) break;

			spl_tmp = spl_book[splnum];
			spl_book[splnum] = spl_book[othnum];
			spl_book[othnum] = spl_tmp;
		}
	}
	return 0;
}

/* splaction = SPELLMENU_CAST, SPELLMENU_VIEW, or spl_book[] index */
static boolean dospellmenu(const char *prompt, int splaction, int *spell_no) {
	winid tmpwin;
	int i, n, how;
	char buf[BUFSZ];
	menu_item *selected;
	anything any;

	tmpwin = create_nhwindow(NHW_MENU);
	start_menu(tmpwin);
	any.a_void = 0; /* zero out all bits */

	/*
	 * The correct spacing of the columns depends on the
	 * following that (1) the font is monospaced and (2)
	 * that selection letters are pre-pended to the given
	 * string and are of the form "a - ".
	 *
	 * To do it right would require that we implement columns
	 * in the window-ports (say via a tab character).
	 */
	if (!iflags.menu_tab_sep)
		sprintf(buf, "%-20s     Level  %-12s Fail  Memory", "    Name", "Category");
	else
		sprintf(buf, "Name\tLevel\tCategory\tFail\tMemory");
	if (flags.menu_style == MENU_TRADITIONAL)
		strcat(buf, iflags.menu_tab_sep ? "\tKey" : "  Key");
	add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_BOLD, buf, MENU_UNSELECTED);
	for (i = 0; i < MAXSPELL && spellid(i) != NO_SPELL; i++) {
		sprintf(buf, iflags.menu_tab_sep ? "%s\t%-d%s\t%s\t%-d%%\t %3d%%" : "%-20s  %2d%s   %-12s %3d%%   %3d%%",
			spellname(i), spellev(i),
			spellknow(i) ? " " : "*",
			spelltypemnemonic(spell_skilltype(spellid(i))),
			100 - percent_success(i),
			(spellknow(i) * 100 + (KEEN - 1)) / KEEN);
		if (flags.menu_style == MENU_TRADITIONAL)
			sprintf(eos(buf), iflags.menu_tab_sep ? "\t%c" : "%4c ", spellet(i) ? spellet(i) : ' ');

		any.a_int = i + 1; /* must be non-zero */
		add_menu(tmpwin, NO_GLYPH, &any,
			 0, 0, ATR_NONE, buf,
			 (i == splaction) ? MENU_SELECTED : MENU_UNSELECTED);
	}
	end_menu(tmpwin, prompt);

	how = PICK_ONE;
	if (splaction == SPELLMENU_VIEW && spellid(1) == NO_SPELL)
		how = PICK_NONE; /* only one spell => nothing to swap with */
	n = select_menu(tmpwin, how, &selected);
	destroy_nhwindow(tmpwin);
	if (n > 0) {
		*spell_no = selected[0].item.a_int - 1;
		/* menu selection for `PICK_ONE' does not
		   de-select any preselected entry */
		if (n > 1 && *spell_no == splaction)
			*spell_no = selected[1].item.a_int - 1;
		free(selected);
		/* default selection of preselected spell means that
		   user chose not to swap it with anything */
		if (*spell_no == splaction) return false;
		return true;
	} else if (splaction >= 0) {
		/* explicit de-selection of preselected spell means that
		   user is still swapping but not for the current spell */
		*spell_no = splaction;
		return true;
	}
	return false;
}

/* Integer square root function without using floating point.
 * This could be replaced by a faster algorithm, but has not because:
 * + the simple algorithm is easy to read
 * + this algorithm does not require 64-bit support
 * + in current usage, the values passed to isqrt() are not really that
 *   large, so the performance difference is negligible
 * + isqrt() is used in only one place
 * + that one place is not the bottle-neck
 */
static int isqrt(int val) {
	int rt = 0;
	int odd = 1;
	while (val >= odd) {
		val = val - odd;
		odd = odd + 2;
		rt = rt + 1;
	}
	return rt;
}

static int percent_success(int spell) {
	/* Intrinsic and learned ability are combined to calculate
	 * the probability of player's success at cast a given spell.
	 */
	int chance, splcaster, special, statused;
	int difficulty;
	int skill;

	splcaster = urole.spelbase;
	special = urole.spelheal;
	statused = ACURR(urole.spelstat);

	/* Calculate armor penalties */
	if (uarm && !(uarm->otyp == ROBE ||
		      uarm->otyp == ROBE_OF_POWER ||
		      uarm->otyp == ROBE_OF_PROTECTION))
		splcaster += 5;

	/* Robes are body armour in SLASH'EM */
	if (uarm && is_metallic(uarm))
		splcaster += urole.spelarmr;
	if (uarms) splcaster += urole.spelshld;

	if (uarmh && is_metallic(uarmh) && uarmh->otyp != HELM_OF_BRILLIANCE)
		splcaster += uarmhbon;
	if (uarmg && is_metallic(uarmg)) splcaster += uarmgbon;
	if (uarmf && is_metallic(uarmf)) splcaster += uarmfbon;

	if (spellid(spell) == urole.spelspec)
		splcaster += urole.spelsbon;

	/* `healing spell' bonus */
	if (spell_skilltype(spellid(spell)) == P_HEALING_SPELL)
		splcaster += special;

	if (uarm && uarm->otyp == ROBE_OF_POWER) splcaster -= 3;
	if (splcaster < 5) splcaster = 5;
	if (splcaster > 20) splcaster = 20;

	/* Calculate learned ability */

	/* Players basic likelihood of being able to cast any spell
	 * is based of their `magic' statistic. (Int or Wis)
	 */
	chance = 11 * statused / 2;

	/*
	 * High level spells are harder.  Easier for higher level casters.
	 * The difficulty is based on the hero's level and their skill level
	 * in that spell type.
	 */
	skill = P_SKILL(spell_skilltype(spellid(spell)));
	skill = max(skill, P_UNSKILLED) - 1; /* unskilled => 0 */
	difficulty = (spellev(spell) - 1) * 4 - ((skill * 6) + (u.ulevel / 3) + 1);

	if (difficulty > 0) {
		/* Player is too low level or unskilled. */
		chance -= isqrt(900 * difficulty + 2000);
	} else {
		/* Player is above level.  Learning continues, but the
		 * law of diminishing returns sets in quickly for
		 * low-level spells.  That is, a player quickly gains
		 * no advantage for raising level.
		 */
		int learning = 15 * -difficulty / spellev(spell);
		chance += learning > 20 ? 20 : learning;
	}

	/* Clamp the chance: >18 stat and advanced learning only help
	 * to a limit, while chances below "hopeless" only raise the
	 * specter of overflowing 16-bit ints (and permit wearing a
	 * shield to raise the chances :-).
	 */
	if (chance < 0) chance = 0;
	if (chance > 120) chance = 120;

	/* Wearing anything but a light shield makes it very awkward
	 * to cast a spell.  The penalty is not quite so bad for the
	 * player's class-specific spell.
	 */
	if (uarms && weight(uarms) > (int)objects[SMALL_SHIELD].oc_weight) {
		if (spellid(spell) == urole.spelspec) {
			chance /= 2;
		} else {
			chance /= 4;
		}
	}

	/* Finally, chance (based on player intell/wisdom and level) is
	 * combined with ability (based on player intrinsics and
	 * encumbrances).  No matter how intelligent/wise and advanced
	 * a player is, intrinsics and encumbrance can prevent casting;
	 * and no matter how able, learning is always required.
	 */
	chance = chance * (20 - splcaster) / 15 - splcaster;

	/* Clamp to percentile */
	if (chance > 100) chance = 100;
	if (chance < 0) chance = 0;

	return chance;
}

/* Learn a spell during creation of the initial inventory */
void initialspell(struct obj *obj) {
	int i;

	for (i = 0; i < MAXSPELL; i++) {
		if (spellid(i) == obj->otyp) {
			pline("Error: Spell %s already known.",
			      OBJ_NAME(objects[obj->otyp]));
			return;
		}
		if (spellid(i) == NO_SPELL) {
			spl_book[i].sp_id = obj->otyp;
			spl_book[i].sp_lev = objects[obj->otyp].oc_level;
			incrnknow(i);
			return;
		}
	}
	impossible("Too many spells memorized!");
	return;
}

boolean studyspell() {
	/*Vars are for studying spells 'W', 'F', 'I', 'N'*/
	int spell_no;

	if (getspell(&spell_no)) {
		if (spellknow(spell_no) <= 0) {
			pline("You are unable to focus your memory of the spell.");
			return false;
		} else if (spellknow(spell_no) <= 1000) {
			pline("Your focus and reinforce your memory of the spell.");
			incrnknow(spell_no);
			exercise(A_WIS, true); /* extra study */
			return true;
		} else /* 1000 < spellknow(spell_no) <= 5000 */
			pline("You know that spell quite well already.");
	}
	return false;
}

/*spell.c*/
