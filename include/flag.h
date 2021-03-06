/*	SCCS Id: @(#)flag.h	3.4	2002/08/22	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

/* If you change the flag structure make sure you increment EDITLEVEL in   */
/* patchlevel.h if needed.  Changing the instance_flags structure does	   */
/* not require incrementing EDITLEVEL.					   */

#ifndef FLAG_H
#define FLAG_H

/*
 * Persistent flags that are saved and restored with the game.
 *
 */

struct flag {
	bool acoustics; // allow dungeon sound messages
	bool autodig; /* MRKR: Automatically dig */
	bool beginner;
#ifdef MAIL
	bool biff; /* enable checking for mail */
#endif
	bool confirm; /* confirm before hitting tame monsters */
	bool debug;   /* in debugging mode */
#define wizard flags.debug
	bool end_own; /* list all own scores */
	bool explore; /* in exploration mode */
#define discover flags.explore
	bool female;
	bool friday13;	   /* it's Friday the 13th */
	bool groundhogday; /* KMH -- February 2 */
	bool help;	   /* look in data file for info about stuff */
	bool ignintr;	   /* ignore interrupts */
#ifdef INSURANCE
	bool ins_chkpt; /* checkpoint as appropriate */
#endif
	bool invlet_constant; /* let objects keep their inventory symbol */
	bool invweight;	      /* show weight in inventory and when picking up */

	/*WAC keep_save option*/
#ifdef KEEP_SAVE
	bool keep_savefile; /* Keep Old Save files*/
#endif
	bool legacy;	   /* print game entry "story" */
	bool lit_corridor; /* show a dark corr as lit if it is in sight */
	bool menu_on_esc; /* show menu when hitting esc */
	bool nap;      /* `timed_delay' option for display effects */
#ifdef MAC
	bool page_wait; /* put up a --More-- after a page of messages */
#endif
	bool perm_invent;   /* keep full inventories up until dismissed */
	bool pickup;	    /* whether you pickup or move and look */
	bool pickup_thrown; /* auto-pickup items you threw */

	bool pushweapon;    /* When wielding, push old weapon into second slot */
	bool rest_on_space; /* space means rest */
	bool safe_dog;	    /* give complete protection to the dog */
	bool showexp;	    /* show experience points */
	bool showscore;	    /* show score */
	bool showdmg;	    /* show damage */
	bool showweight;    /* show weight on status line */
	bool silent;	    /* whether the bell rings or not */
	bool sortpack;	    /* sorted inventory */
	bool sparkle;	    /* show "resisting" special FX (Scott Bigham) */
	bool standout;	    /* use standout for --More-- */
	bool time;	    /* display elapsed 'time' */
	bool tombstone;	    /* print tombstone */
	bool toptenwin;	    /* ending list in window instead of stdout */
	bool use_menu_glyphs;
	bool verbose;		 /* max battle info */
	bool prayconfirm;	 /* confirm before praying */
	int end_top, end_around; /* describe desired score list */
	unsigned moonphase;
#define NEW_MOON  0
#define FULL_MOON 4
	int pickup_burden;	       /* maximum burden before prompt */
	/* KMH, role patch -- Variables used during startup.
	 *
	 * If the user wishes to select a role, race, gender, and/or alignment
	 * during startup, the choices should be recorded here.  This
	 * might be specified through command-line options, environmental
	 * variables, a popup dialog box, menus, etc.
	 *
	 * These values are each an index into an array.  They are not
	 * characters or letters, because that limits us to 26 roles.
	 * They are not bools, because someday someone may need a neuter
	 * gender.  Negative values are used to indicate that the user
	 * hasn't yet specified that particular value.	If you determine
	 * that the user wants a random choice, then you should set an
	 * appropriate random value; if you just left the negative value,
	 * the user would be asked again!
	 *
	 * These variables are stored here because the u structure is
	 * cleared during character initialization, and because the
	 * flags structure is restored for saved games.  Thus, we can
	 * use the same parameters to build the role entry for both
	 * new and restored games.
	 *
	 * These variables should not be referred to after the character
	 * is initialized or restored (specifically, after role_init()
	 * is called).
	 */
	int initrole;  /* starting role      (index into roles[])   */
	int initrace;  /* starting race      (index into races[])   */
	int initgend;  /* starting gender    (index into genders[]) */
	int initalign; /* starting alignment (index into aligns[])  */
	int randomall; /* randomly assign everything not specified */
	int pantheon;  /* deity selection for priest character */
	/* KMH, balance patch */
	int boot_count; /* boots from fishing pole */
	char inv_order[MAXOCLASSES];
	char pickup_types[MAXOCLASSES];
#define NUM_DISCLOSURE_OPTIONS	    5
#define DISCLOSE_PROMPT_DEFAULT_YES 'y'
#define DISCLOSE_PROMPT_DEFAULT_NO  'n'
#define DISCLOSE_YES_WITHOUT_PROMPT '+'
#define DISCLOSE_NO_WITHOUT_PROMPT  '-'
	char end_disclose[NUM_DISCLOSURE_OPTIONS + 1]; /* disclose various info
							       upon exit */
	char menu_style;			       /* User interface style setting */
};

/*
 * Flags that are set each time the game is started.
 * These are not saved with the game.
 *
 */

struct instance_flags {
	bool cbreak; /* in cbreak mode, rogue format */
#ifdef CURSES_GRAPHICS
	bool classic_status; /* What kind of horizontal statusbar to use */
#endif
	bool debug_fuzzer;
	bool echo;	      /* 1 to echo characters */
	unsigned msg_history; /* hint: # of top lines to save */
	bool msg_is_alert;    /* need to press an extra key to get rid of a --More-- prompt.  Only in curses */
	bool num_pad;	      /* use numbers for movement commands */
	bool news;	      /* print news */
	bool window_inited;   /* true if init_nhwindows() completed */
	bool vision_inited;   /* true if vision is ready */
	bool menu_tab_sep;    /* Use tabs to separate option menu fields */
	bool menu_requested;  /* Flag for overloaded use of 'm' prefix
				  * on some non-move commands */

	bool in_dumplog;  // doing the dumplog right now?
	uchar num_pad_mode;
	int menu_headings;  /* ATR for menu headings */
	int purge_monsters; /* # of dead monsters still on fmon list */
	int *opt_booldup;   /* for duplication of bool opts in config file */
	int *opt_compdup;   /* for duplication of compound opts in config file */
	coord travelcc;	    /* coordinates for travel_cache */
	bool hilite_hidden_stairs;
	bool hilite_obj_piles;
	bool sanity_check;    /* run sanity checks */
	bool mon_polycontrol; /* debug: control monster polymorphs */
	char prevmsg_window; /* type of old message window to use */
#if defined(TTY_GRAPHICS) || defined(CURSES_GRAPHICS)
	bool extmenu; /* extended commands use menu interface */
#endif
	bool use_menu_color;	/* use color in menus; only if wc_color */
	bool use_status_colors; /* use color in status line; only if wc_color */
	bool hitpointbar;
#ifdef WIN32
	bool hassound;	   /* has a sound card */
	bool usesound;	   /* use the sound card */
	bool usepcspeaker; /* use the pc speaker */
	bool tile_view;
	bool over_view;
	bool traditional_view;
#endif
#ifdef LAN_FEATURES
	bool lan_mail;	       /* mail is initialized */
	bool lan_mail_fetched; /* mail is awaiting display */
#endif
	bool quiver_fired;
	int graphics;
	bool use_menu_glyphs; /* item glyphs in inventory */
	/*
	 * Window capability support.
	 */
	bool wc_color;		   /* use color graphics                  */
	bool wc_hilite_pet;	   /* hilight pets                        */
	bool wc_tiled_map;	   /* show map using tiles                */
	bool wc_preload_tiles;	   /* preload tiles into memory           */
	int wc_tile_width;	   /* tile width                          */
	int wc_tile_height;	   /* tile height                         */
	char *wc_tile_file;	   /* name of tile file;overrides default */
	bool wc_inverse;	   /* use inverse video for some things   */
	int wc_align_status;	   /*  status win at top|bot|right|left   */
	int wc_align_message;	   /* message win at top|bot|right|left   */
	int wc_vary_msgcount;	   /* show more old messages at a time    */
	char *wc_foregrnd_menu;	   /* points to foregrnd color name for menu win   */
	char *wc_backgrnd_menu;	   /* points to backgrnd color name for menu win   */
	char *wc_foregrnd_message; /* points to foregrnd color name for msg win    */
	char *wc_backgrnd_message; /* points to backgrnd color name for msg win    */
	char *wc_foregrnd_status;  /* points to foregrnd color name for status win */
	char *wc_backgrnd_status;  /* points to backgrnd color name for status win */
	char *wc_foregrnd_text;	   /* points to foregrnd color name for text win   */
	char *wc_backgrnd_text;	   /* points to backgrnd color name for text win   */
	char *wc_font_map;	   /* points to font name for the map win */
	char *wc_font_message;	   /* points to font name for message win */
	char *wc_font_status;	   /* points to font name for status win  */
	char *wc_font_menu;	   /* points to font name for menu win    */
	char *wc_font_text;	   /* points to font name for text win    */
	int wc_fontsiz_map;	   /* font size for the map win           */
	int wc_fontsiz_message;	   /* font size for the message window    */
	int wc_fontsiz_status;	   /* font size for the status window     */
	int wc_fontsiz_menu;	   /* font size for the menu window       */
	int wc_fontsiz_text;	   /* font size for text windows          */
	int wc_scroll_amount;	   /* scroll this amount at scroll_margin */
	int wc_scroll_margin;	   /* scroll map when this far from
						the edge */
	int wc_map_mode;	   /* specify map viewing options, mostly
						for backward compatibility */
	int wc_player_selection;   /* method of choosing character */
	bool wc_splash_screen;	   /* display an opening splash screen or not */
	bool wc_popup_dialog;	   /* put queries in pop up dialogs instead of
				   		in the message window */
	bool wc_mouse_support;	   /* allow mouse support */
	bool wc2_fullscreen;	   /* run fullscreen */
	bool wc2_softkeyboard;	   /* use software keyboard */
	bool wc2_wraptext;	   /* wrap text */
	int wc2_term_cols;	   /* terminal width, in characters */
	int wc2_term_rows;	   /* terminal height, in characters */
	int wc2_windowborders;	   /* display borders on NetHack windows */
	int wc2_petattr;	   /* points to text attributes for pet */
	bool wc2_guicolor;	   /* allow colors in GUI (outside map) */

	bool cmdassist; /* provide detailed assistance for some commands */
	bool clicklook;
	bool obsolete;	/* obsolete options can point at this, it isn't used */
	/* Items which belong in flags, but are here to allow save compatibility */
	bool lootabc;	/* use "a/b/c" rather than "o/i/b" when looting */
	bool showrace;	/* show hero glyph by race rather than by role */
	int runmode;	/* update screen display during run moves */
	struct autopickup_exception *autopickup_exceptions[2];
#define AP_LEAVE 0
#define AP_GRAB	 1
	bool showrealtime; /* show actual elapsed time */
};

/*
 * Old deprecated names
 */
#define use_color   wc_color
#define hilite_pet  wc_hilite_pet
#define use_inverse wc_inverse
#ifdef MAC
#define popup_dialog wc_popup_dialog
#endif
#define preload_tiles wc_preload_tiles

extern struct flag flags;
extern struct instance_flags iflags;

/* runmode options */
#define RUN_TPORT 0 /* don't update display until movement stops */
#define RUN_LEAP  1 /* update display every 7 steps */
#define RUN_STEP  2 /* update display every single step */
#define RUN_CRAWL 3 /* walk w/ extra delay after each update */

#endif /* FLAG_H */
