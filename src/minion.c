/*	SCCS Id: @(#)minion.c	3.4	2003/01/09	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

void newemin(struct monst *mtmp) {
	if (!mtmp->mextra) mtmp->mextra = newmextra();
	if (!EMIN(mtmp)) {
		EMIN(mtmp) = new(struct emin);
	}
}

void free_emin(struct monst *mtmp) {
	if (mtmp->mextra && EMIN(mtmp)) {
		free(EMIN(mtmp));
		EMIN(mtmp) = NULL;
	}
	mtmp->isminion = 0;
}

// mon summons a monster
int msummon(struct monst *mon) {
	struct permonst *ptr;
	int dtype = NON_PM, cnt = 0, result = 0;
	aligntyp atyp;
	struct monst *mtmp;

	if (mon) {
		ptr = mon->data;
		atyp = mon->ispriest ? EPRI(mon)->shralign :
		       mon->isminion ? EMIN(mon)->min_align :
		       (ptr->maligntyp == A_NONE) ? A_NONE : sgn(ptr->maligntyp);

	} else {
		ptr = &mons[PM_WIZARD_OF_YENDOR];
		atyp = (ptr->maligntyp == A_NONE) ? A_NONE : sgn(ptr->maligntyp);
	}

	if (is_dprince(ptr) || (ptr == &mons[PM_WIZARD_OF_YENDOR])) {
		dtype = (!rn2(20)) ? dprince(atyp) :
				     (!rn2(4)) ? dlord(atyp) : ndemon(atyp);
		cnt = (!rn2(4) && is_ndemon(&mons[dtype])) ? 2 : 1;
	} else if (is_dlord(ptr)) {
		dtype = (!rn2(50)) ? dprince(atyp) :
				     (!rn2(20)) ? dlord(atyp) : ndemon(atyp);
		cnt = (!rn2(4) && is_ndemon(&mons[dtype])) ? 2 : 1;
	} else if (is_ndemon(ptr)) {
		dtype = (!rn2(20)) ? dlord(atyp) :
				     (!rn2(6)) ? ndemon(atyp) : monsndx(ptr);
		cnt = 1;
	} else if (is_lminion(mon)) {
		dtype = (is_lord(ptr) && !rn2(20)) ? llord() :
						     (is_lord(ptr) || !rn2(6)) ? lminion() : monsndx(ptr);
		cnt = (!rn2(4) && !is_lord(&mons[dtype])) ? 2 : 1;
	} else if (ptr == &mons[PM_ANGEL]) {
		/* non-lawful angels can also summon */
		if (!rn2(6)) {
			switch (atyp) { /* see summon_minion */
				case A_NEUTRAL:
					dtype = PM_AIR_ELEMENTAL + rn2(4);
					break;
				case A_CHAOTIC:
				case A_NONE:
					dtype = ndemon(atyp);
					break;
			}
		} else {
			dtype = PM_ANGEL;
		}
		cnt = (!rn2(4) && !is_lord(&mons[dtype])) ? 2 : 1;
	}

	if (dtype == NON_PM) return 0;

	/* sanity checks */
	if (cnt > 1 && (mons[dtype].geno & G_UNIQ)) cnt = 1;
	/*
	 * If this daemon is unique and being re-summoned (the only way we
	 * could get this far with an extinct dtype), try another.
	 */
	if (mvitals[dtype].mvflags & G_GONE) {
		dtype = ndemon(atyp);
		if (dtype == NON_PM) return 0;
	}

	while (cnt > 0) {
		mtmp = makemon(&mons[dtype], u.ux, u.uy, MM_EMIN);
		if (mtmp) {
			result++;
			/* an angel's alignment should match the summoner */
			if (dtype == PM_ANGEL) {
				mtmp->isminion = 1;
				EMIN(mtmp)->min_align = atyp;
				/* renegade if same alignment but not peaceful
				   or peaceful but different alignment */
				EMIN(mtmp)->renegade = (atyp != u.ualign.type) ^ !mtmp->mpeaceful;
			}

		}
		cnt--;
	}

	return result;
}

void summon_minion(aligntyp alignment, boolean talk) {
	struct monst *mon;
	int mnum;

	switch ((int)alignment) {
		case A_LAWFUL:
			mnum = lminion();
			break;
		case A_NEUTRAL:
			mnum = PM_AIR_ELEMENTAL + rn2(4);
			break;
		case A_CHAOTIC:
		case A_NONE:
			mnum = ndemon(alignment);
			break;
		default:
			impossible("unaligned player?");
			mnum = ndemon(A_NONE);
			break;
	}
	if (mnum == NON_PM) {
		mon = 0;
	} else if (mnum == PM_ANGEL) {
		mon = makemon(&mons[mnum], u.ux, u.uy, MM_EPRI|MM_EMIN);
		if (mon) {
			mon->isminion = true;
			EMIN(mon)->min_align = alignment;
			EMIN(mon)->renegade = false;
			EPRI(mon)->shralign = alignment; /* always A_LAWFUL here */
		}
	} else if (mnum != PM_SHOPKEEPER && mnum != PM_GUARD
			&& mnum != PM_ALIGNED_PRIEST && mnum != PM_HIGH_PRIEST) {
		/* This was mons[mnum].pxlth == 0 but is this restriction
		 * appropriate or necessary now that the structures are separate? */
		mon = makemon(&mons[mnum], u.ux, u.uy, MM_EMIN);
		if (mon) {
			mon->isminion = 1;
			EMIN(mon)->min_align = alignment;
			EMIN(mon)->renegade = false;
		}
	} else {
		mon = makemon(&mons[mnum], u.ux, u.uy, NO_MM_FLAGS);
	}
	if (mon) {
		if (talk) {
			pline("The voice of %s booms:", align_gname(alignment));
			verbalize("Thou shalt pay for thine indiscretion!");
			if (!Blind)
				pline("%s appears before you.", Amonnam(mon));
		}
		mon->mpeaceful = false;
		/* don't call set_malign(); player was naughty */
	}
}
#define Athome (Inhell && mtmp->cham == CHAM_ORDINARY)

// returns true if it won't attack.
bool demon_talk(struct monst *mtmp) {
	long cash, demand, offer;

	if (uwep && uwep->oartifact == ART_EXCALIBUR) {
		pline("%s looks very angry.", Amonnam(mtmp));
		mtmp->mpeaceful = false;
		mtmp->mtame = 0;
		set_malign(mtmp);
		newsym(mtmp->mx, mtmp->my);
		return false;
	}

	/* Slight advantage given. */
	if (is_dprince(mtmp->data) && mtmp->minvis) {
		mtmp->minvis = mtmp->perminvis = 0;
		if (!Blind) pline("%s appears before you.", Amonnam(mtmp));
		newsym(mtmp->mx, mtmp->my);
	}
	if (youmonst.data->mlet == S_DEMON) { /* Won't blackmail their own. */
		pline("%s says, \"Good hunting, %s.\"",
		      Amonnam(mtmp), flags.female ? "Sister" : "Brother");
		if (!tele_restrict(mtmp)) rloc(mtmp, false);
		return 1;
	}
	cash = money_cnt(invent);
	demand = (cash * (rnd(80) + 20 * Athome)) /
		 (100 * (1 + (sgn(u.ualign.type) == sgn(mtmp->data->maligntyp))));

	if (!demand) { /* you have no gold */
		mtmp->mpeaceful = 0;
		set_malign(mtmp);
		return false;
	} else {
		/* make sure that the demand is unmeetable if the monster
		   has the Amulet, preventing monster from being satisified
		   and removed from the game (along with said Amulet...) */
		if (mon_has_amulet(mtmp))
			demand = cash + (long)rn1(1000, 40);

		pline("%s demands %ld %s for safe passage.",
		      Amonnam(mtmp), demand, currency(demand));

		if ((offer = bribe(mtmp)) >= demand) {
			pline("%s vanishes, laughing about cowardly mortals.",
			      Amonnam(mtmp));
		} else if (offer > 0L && (long)rnd(40) > (demand - offer)) {
			pline("%s scowls at you menacingly, then vanishes.",
			      Amonnam(mtmp));
		} else {
			pline("%s gets angry...", Amonnam(mtmp));
			mtmp->mpeaceful = 0;
			set_malign(mtmp);
			return false;
		}
	}
	mongone(mtmp);
	return true;
}

/* this routine returns the # of an appropriate minion,
   given a difficulty rating from 1 to 30 */
int lawful_minion(int difficulty) {
	difficulty = difficulty + rn2(5) - 2;
	if (difficulty < 0) difficulty = 0;
	if (difficulty > 30) difficulty = 30;
	difficulty /= 3;
	switch (difficulty) {
		case 0:
			return PM_TENGU;
		case 1:
			return PM_COUATL;
		case 2:
			return PM_WHITE_UNICORN;
		case 3:
			return PM_MOVANIC_DEVA;
		case 4:
			return PM_MONADIC_DEVA;
		case 5:
			return PM_KI_RIN;
		case 6:
			return PM_ASTRAL_DEVA;
		case 7:
			return PM_ARCHON;
		case 8:
			return PM_PLANETAR;
		case 9:
			return PM_SOLAR;
		case 10:
			return PM_SOLAR;

		default:
			return PM_TENGU;
	}
}

/* this routine returns the # of an appropriate minion,
   given a difficulty rating from 1 to 30 */
int neutral_minion(int difficulty) {
	difficulty = difficulty + rn2(9) - 4;
	if (difficulty < 0) difficulty = 0;
	if (difficulty > 30) difficulty = 30;
	if (difficulty < 6) return PM_GRAY_UNICORN;
	if (difficulty < 15) return PM_AIR_ELEMENTAL + rn2(4);
	return PM_DJINNI /* +rn2(4) */;
}

/* this routine returns the # of an appropriate minion,
   given a difficulty rating from 1 to 30 */
int chaotic_minion(int difficulty) {
	difficulty = difficulty + rn2(5) - 2;
	if (difficulty < 0) difficulty = 0;
	if (difficulty > 30) difficulty = 30;
	/* KMH, balance patch -- avoid using floating-point (not supported by all ports) */
	/*   difficulty = (int)((float)difficulty / 1.5);*/
	difficulty = (difficulty * 2) / 3;
	switch (difficulty) {
		case 0:
			return PM_GREMLIN;
		case 1:
		case 2:
			return PM_DRETCH + rn2(5);
		case 3:
			return PM_BLACK_UNICORN;
		case 4:
			return PM_BLOOD_IMP;
		case 5:
			return PM_SPINED_DEVIL;
		case 6:
			return PM_SHADOW_WOLF;
		case 7:
			return PM_HELL_HOUND;
		case 8:
			return PM_HORNED_DEVIL;
		case 9:
			return PM_BEARDED_DEVIL;
		case 10:
			return PM_BAR_LGURA;
		case 11:
			return PM_CHASME;
		case 12:
			return PM_BARBED_DEVIL;
		case 13:
			return PM_VROCK;
		case 14:
			return PM_BABAU;
		case 15:
			return PM_NALFESHNEE;
		case 16:
			return PM_MARILITH;
		case 17:
			return PM_NABASSU;
		case 18:
			return PM_BONE_DEVIL;
		case 19:
			return PM_ICE_DEVIL;
		case 20:
			return PM_PIT_FIEND;
	}
	return PM_GREMLIN;
}

long bribe(struct monst *mtmp) {
	char buf[BUFSZ];
	long offer;
	long umoney = money_cnt(invent);

	getlin("How much will you offer?", buf);
	if (sscanf(buf, "%ld", &offer) != 1) offer = 0L;

	/*Michael Paddon -- fix for negative offer to monster*/
	/*JAR880815 - */
	if (offer < 0L) {
		pline("You try to shortchange %s, but fumble.",
		      mon_nam(mtmp));
		return 0L;
	} else if (offer == 0L) {
		pline("You refuse.");
		return 0L;
	} else if (offer >= umoney) {
		pline("You give %s all your money.", mon_nam(mtmp));
		offer = umoney;
	} else {
		pline("You give %s %ld %s.", mon_nam(mtmp), offer, currency(offer));
	}
	money2mon(mtmp, offer);
	context.botl = 1;
	return offer;
}

int dprince(aligntyp atyp) {
	int tryct, pm;

	for (tryct = 0; tryct < 20; tryct++) {
		pm = rn1(PM_DEMOGORGON + 1 - PM_ORCUS, PM_ORCUS);
		if (!(mvitals[pm].mvflags & G_GONE) &&
		    (atyp == A_NONE || sgn(mons[pm].maligntyp) == sgn(atyp)))
			return pm;
	}
	return dlord(atyp); /* approximate */
}

int dlord(aligntyp atyp) {
	int tryct, pm;

	for (tryct = 0; tryct < 20; tryct++) {
		pm = rn1(PM_YEENOGHU + 1 - PM_JUIBLEX, PM_JUIBLEX);
		if (!(mvitals[pm].mvflags & G_GONE) &&
		    (atyp == A_NONE || sgn(mons[pm].maligntyp) == sgn(atyp)))
			return pm;
	}
	return ndemon(atyp); /* approximate */
}

// create lawful (good) lord
int llord(void) {
	if (!(mvitals[PM_ARCHON].mvflags & G_GONE))
		return PM_ARCHON;

	return lminion(); /* approximate */
}

int lminion(void) {
	int tryct;
	struct permonst *ptr;

	for (tryct = 0; tryct < 20; tryct++) {
		ptr = mkclass(S_ANGEL, 0);
		if (ptr && !is_lord(ptr))
			return monsndx(ptr);
	}

	return NON_PM;
}

int ndemon(aligntyp atyp) {
	int tryct;
	struct permonst *ptr;

	for (tryct = 0; tryct < 20; tryct++) {
		ptr = mkclass(S_DEMON, 0);
		if (ptr && is_ndemon(ptr) &&
		    (atyp == A_NONE || sgn(ptr->maligntyp) == sgn(atyp)))
			return monsndx(ptr);
	}

	return NON_PM;
}

/*minion.c*/
