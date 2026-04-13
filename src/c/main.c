/**
 * Skull — Bluffing card game for Pebble
 * Targets: emery (Time 2), gabbro (Round 2)
 *
 * 3-6 players. Each has 3 roses + 1 skull. Play cards face-down,
 * then bet on how many roses you can flip. Flip a skull = lose a card.
 * Win 2 rounds to win the game.
 */

#include <pebble.h>
#include <stdlib.h>

#define MAX_PLAYERS 6
#define MAX_CARDS   4
#define NUM_TOKENS  6
#define CARD_ROSE   0
#define CARD_SKULL  1

enum {
  ST_SETUP, ST_ORDER, ST_PASS, ST_PLAY, ST_BET,
  ST_REVEAL_OWN, ST_REVEAL_PICK, ST_FLIP,
  ST_SKULL, ST_WIN_ROUND, ST_GAMEOVER
};

typedef struct {
  int  total_roses;    // permanent (starts 3)
  bool has_skull;      // permanent
  int  hand_roses;     // available in hand this round
  bool hand_skull;     // available in hand this round
  int  stack[MAX_CARDS]; // played cards bottom→top
  int  stack_count;
  int  revealed;       // cards revealed from top
  int  points;
  int  icon;
  bool eliminated;
} Player;

static const char *s_tok_name[] = {
  "Star","Heart","Diamond","Circle","Square","Bolt"
};
static const char *s_tok_char[] = {
  "\xEF\x80\x85","\xEF\x80\x84","\xEF\x88\x99",
  "\xEF\x84\x91","\xEF\x83\x88","\xEF\x83\xA7",
};

// ============================================================================
// GLOBALS
// ============================================================================
static Window *s_win;
static Layer  *s_canvas;
static GFont   s_icon_font_20, s_icon_font_14;

static int s_state = ST_SETUP;
static int s_num_players;
static int s_setup_cursor = 0;
static int s_cursor = 0;

static Player s_players[MAX_PLAYERS];
static int    s_order[MAX_PLAYERS];
static int    s_cur_idx;         // index into s_order

// Round tracking
static int  s_round_starter;     // who starts each round
static bool s_all_played_one;    // everyone played at least 1
static bool s_in_bet_phase;

// Bet tracking
static int  s_current_bet;
static int  s_bettor_idx;        // index into s_order
static bool s_bet_passed[MAX_PLAYERS];
static int  s_bet_pick;          // number currently selected in bet UI
static bool s_is_first_bettor;   // first person to bet (no pass option)

// Reveal tracking
static int  s_roses_found;
static int  s_reveal_target;     // player being revealed
static int  s_flip_card;         // card being shown (CARD_ROSE or CARD_SKULL)
static int  s_skull_owner;       // who owned the skull

// ============================================================================
// COLORS
// ============================================================================
#ifdef PBL_COLOR
static GColor tok_color(int t) {
  switch(t) {
    case 0: return GColorYellow;
    case 1: return GColorRed;
    case 2: return GColorCyan;
    case 3: return GColorGreen;
    case 4: return GColorOrange;
    default: return GColorPurple;
  }
}
#endif

// ============================================================================
// HELPERS
// ============================================================================
static int cp(void) { return s_order[s_cur_idx]; }

static void draw_token(GContext *ctx, int cx, int cy, int icon, bool lg) {
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, tok_color(icon));
  #else
  graphics_context_set_text_color(ctx, GColorWhite);
  #endif
  GFont f = lg ? s_icon_font_20 : s_icon_font_14;
  int sz = lg ? 30 : 22;
  if(!f) {
    f = fonts_get_system_font(lg ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD);
    char nm[2] = {s_tok_name[icon][0], 0};
    graphics_draw_text(ctx, nm, f, GRect(cx-sz/2, cy-sz/2, sz, sz),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }
  graphics_draw_text(ctx, s_tok_char[icon], f, GRect(cx-sz/2, cy-sz/2, sz, sz),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Small rose icon
static void draw_rose(GContext *ctx, int cx, int cy) {
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_circle(ctx, GPoint(cx, cy), 4);
  graphics_context_set_fill_color(ctx, GColorFromHEX(0xFF5555));
  graphics_fill_circle(ctx, GPoint(cx-1, cy-1), 2);
  #else
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(cx, cy), 4);
  #endif
}

// Small skull icon
static void draw_skull_icon(GContext *ctx, int cx, int cy) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(cx, cy-1), 5);
  graphics_fill_rect(ctx, GRect(cx-3, cy+2, 6, 3), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(cx-2, cy-2), 1);
  graphics_fill_circle(ctx, GPoint(cx+2, cy-2), 1);
  #ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx, GColorBlack);
  #endif
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(cx-2, cy+3), GPoint(cx+2, cy+3));
  graphics_draw_line(ctx, GPoint(cx, cy+2), GPoint(cx, cy+4));
}

// Card back (face-down)
static void draw_card_back(GContext *ctx, int x, int cy, bool hl) {
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, hl ? GColorFromHEX(0x555555) : GColorFromHEX(0x333333));
  #else
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  #endif
  graphics_fill_rect(ctx, GRect(x, cy-7, 10, 14), 2, GCornersAll);
}

static int next_active(int from) {
  int p = from;
  do { p = (p + 1) % s_num_players; }
  while(s_players[s_order[p]].eliminated && p != from);
  return p;
}

static int total_on_table(void) {
  int t = 0;
  for(int i = 0; i < s_num_players; i++)
    if(!s_players[i].eliminated) t += s_players[i].stack_count;
  return t;
}

static int active_count(void) {
  int c = 0;
  for(int i = 0; i < s_num_players; i++)
    if(!s_players[i].eliminated) c++;
  return c;
}

static bool everyone_played_one(void) {
  for(int i = 0; i < s_num_players; i++)
    if(!s_players[i].eliminated && s_players[i].stack_count == 0) return false;
  return true;
}

static int player_hand_count(int pi) {
  return s_players[pi].hand_roses + (s_players[pi].hand_skull ? 1 : 0);
}

static void shuffle_order(void) {
  int icons[NUM_TOKENS];
  for(int i = 0; i < NUM_TOKENS; i++) icons[i] = i;
  for(int i = NUM_TOKENS - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int t = icons[i]; icons[i] = icons[j]; icons[j] = t;
  }
  for(int i = 0; i < s_num_players; i++) {
    s_players[i].icon = icons[i];
    s_order[i] = i;
  }
  for(int i = s_num_players - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int t = s_order[i]; s_order[i] = s_order[j]; s_order[j] = t;
  }
}

static void init_players(void) {
  for(int i = 0; i < s_num_players; i++) {
    s_players[i].total_roses = 3;
    s_players[i].has_skull = true;
    s_players[i].points = 0;
    s_players[i].eliminated = false;
  }
}

static void start_round(void) {
  for(int i = 0; i < s_num_players; i++) {
    Player *p = &s_players[i];
    p->hand_roses = p->total_roses;
    p->hand_skull = p->has_skull;
    p->stack_count = 0;
    p->revealed = 0;
  }
  s_all_played_one = false;
  s_in_bet_phase = false;
  s_current_bet = 0;
  s_roses_found = 0;
  for(int i = 0; i < MAX_PLAYERS; i++) s_bet_passed[i] = false;
  s_cur_idx = s_round_starter;
  // Skip eliminated players
  if(s_players[cp()].eliminated) s_cur_idx = next_active(s_cur_idx);
}

static void lose_random_card(int pi) {
  Player *p = &s_players[pi];
  int total = p->total_roses + (p->has_skull ? 1 : 0);
  if(total <= 0) return;
  int pick = rand() % total;
  if(pick < p->total_roses) p->total_roses--;
  else p->has_skull = false;
  if(p->total_roses == 0 && !p->has_skull) p->eliminated = true;
}

// ============================================================================
// TABLE DRAWING
// ============================================================================
static void draw_table(GContext *ctx, int w, int h, int top_y, int hl_idx,
                       bool show_cursor) {
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  int pad = PBL_IF_ROUND_ELSE(18, 4);
  int lx = PBL_IF_ROUND_ELSE(pad + 14, pad + 4);
  int cols = 2;
  int rows = (s_num_players + 1) / 2;
  int cell_w = (w - lx * 2) / cols;
  int cell_h = 32;

  for(int i = 0; i < s_num_players; i++) {
    int pi = s_order[i];
    Player *p = &s_players[pi];
    int col = i % cols, row = i / cols;
    int cx = lx + col * cell_w + 16;
    int cy = top_y + row * cell_h + cell_h / 2;

    // Highlight
    if(show_cursor && i == hl_idx) {
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, GColorFromHEX(0x002200));
      graphics_fill_rect(ctx, GRect(lx + col * cell_w, cy - cell_h/2 + 1,
        cell_w, cell_h - 2), 4, GCornersAll);
      #endif
    }

    if(p->eliminated) {
      draw_token(ctx, cx, cy, p->icon, false);
      graphics_context_set_text_color(ctx, GColorDarkGray);
      graphics_draw_text(ctx, "OUT", f_sm,
        GRect(cx + 12, cy - 8, 40, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      continue;
    }

    draw_token(ctx, cx, cy, p->icon, false);

    // Point marker
    if(p->points > 0) {
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, GColorYellow);
      graphics_fill_circle(ctx, GPoint(cx + 10, cy - 8), 3);
      #endif
    }

    // Card count + stack
    if(p->stack_count > 0) {
      char cnt[4]; snprintf(cnt, sizeof(cnt), "%d", p->stack_count);
      graphics_context_set_text_color(ctx, GColorWhite);
      graphics_draw_text(ctx, cnt, f_sm,
        GRect(cx + 12, cy - 9, 14, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

      int sx = cx + 26;
      for(int j = 0; j < p->stack_count; j++) {
        int card_x = sx + j * 8;
        if(j < p->stack_count - p->revealed) {
          draw_card_back(ctx, card_x, cy, false);
        } else {
          int si = j;
          if(p->stack[si] == CARD_ROSE) {
            #ifdef PBL_COLOR
            graphics_context_set_fill_color(ctx, GColorRed);
            #else
            graphics_context_set_fill_color(ctx, GColorWhite);
            #endif
            graphics_fill_rect(ctx, GRect(card_x, cy-7, 10, 14), 2, GCornersAll);
          } else {
            graphics_context_set_fill_color(ctx, GColorWhite);
            graphics_fill_rect(ctx, GRect(card_x, cy-7, 10, 14), 2, GCornersAll);
            graphics_context_set_fill_color(ctx, GColorBlack);
            graphics_fill_circle(ctx, GPoint(card_x+3, cy-2), 1);
            graphics_fill_circle(ctx, GPoint(card_x+7, cy-2), 1);
          }
        }
      }
    }
  }
}

// ============================================================================
// CANVAS
// ============================================================================
static void canvas_proc(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  int w = b.size.w, h = b.size.h;
  int pad = PBL_IF_ROUND_ELSE(18, 4);

  // Dark background
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorFromHEX(0x110011));
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // ======== SETUP ========
  if(s_state == ST_SETUP) {
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorRed);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, "SKULL", f_lg,
      GRect(0, h*8/100, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    draw_skull_icon(ctx, w/2, h*8/100 + 50);

    const char *opts[] = {"3 Players","4 Players","5 Players","6 Players"};
    int cy = h * 50 / 100;
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x330011));
    #else
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    #endif
    graphics_fill_rect(ctx, GRect(40, cy+2, w-80, 30), 6, GCornersAll);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, opts[s_setup_cursor], f_lg,
      GRect(0, cy, w, 30),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "SELECT to start", f_sm,
      GRect(0, h*82/100, w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== ORDER ========
  else if(s_state == ST_ORDER) {
    graphics_context_set_text_color(ctx, GColorWhite);
    int ty = PBL_IF_ROUND_ELSE(pad+8, pad+2);
    graphics_draw_text(ctx, "Players", f_lg,
      GRect(0, ty, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int cols = 2, rows_n = (s_num_players+1)/2;
    int cell_w = (w-pad*2)/cols, cell_h = 50;
    int gy = ty + 40;
    for(int i = 0; i < s_num_players; i++) {
      int pi = s_order[i];
      int col = i%cols, row = i/cols;
      int cx = pad + col*cell_w + cell_w/2;
      int cyy = gy + row*cell_h + 18;
      draw_token(ctx, cx, cyy, s_players[pi].icon, true);
    }
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "SELECT to start", f_sm,
      GRect(0, h-PBL_IF_ROUND_ELSE(26,18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== PASS SCREEN ========
  else if(s_state == ST_PASS) {
    int pi = cp();
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    int cy = h/2 - 30;
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "Pass to", f_sm,
      GRect(0, cy, w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    draw_token(ctx, w/2, cy+30, s_players[pi].icon, true);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, tok_color(s_players[pi].icon));
    #endif
    graphics_draw_text(ctx, s_tok_name[s_players[pi].icon], f_md,
      GRect(0, cy+46, w, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "SELECT when ready", f_sm,
      GRect(0, h-PBL_IF_ROUND_ELSE(30,20), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== PLAY CARD ========
  else if(s_state == ST_PLAY) {
    int pi = cp();
    Player *p = &s_players[pi];

    int iy = PBL_IF_ROUND_ELSE(32, 18);
    draw_token(ctx, w/2, iy, p->icon, true);

    // Show hand
    int hy = iy + 24;
    graphics_context_set_text_color(ctx, GColorWhite);
    char hand[24];
    snprintf(hand, sizeof(hand), "%d rose%s%s",
      p->hand_roses, p->hand_roses != 1 ? "s" : "",
      p->hand_skull ? " + skull" : "");
    graphics_draw_text(ctx, hand, f_sm,
      GRect(0, hy, w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    // Options
    int oy = hy + 24;
    int opt = 0;
    int opt_count = 0;
    // Play Rose
    if(p->hand_roses > 0) {
      bool sel = (s_cursor == opt);
      if(sel) {
        #ifdef PBL_COLOR
        graphics_context_set_fill_color(ctx, GColorFromHEX(0x330000));
        graphics_fill_rect(ctx, GRect(pad+20, oy, w-pad*2-40, 24), 4, GCornersAll);
        #endif
      }
      int icon_x = w/2 - 30;
      draw_rose(ctx, icon_x, oy + 12);
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, sel ? GColorYellow : GColorWhite);
      #else
      graphics_context_set_text_color(ctx, sel ? GColorWhite : GColorLightGray);
      #endif
      graphics_draw_text(ctx, "Play Rose", f_md,
        GRect(icon_x + 10, oy + 1, w/2, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      oy += 28;
      opt_count++;
    }
    // Play Skull
    if(p->hand_skull) {
      bool sel = (s_cursor == opt_count);
      if(sel) {
        #ifdef PBL_COLOR
        graphics_context_set_fill_color(ctx, GColorFromHEX(0x330000));
        graphics_fill_rect(ctx, GRect(pad+20, oy, w-pad*2-40, 24), 4, GCornersAll);
        #endif
      }
      int icon_x = w/2 - 30;
      draw_skull_icon(ctx, icon_x, oy + 12);
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, sel ? GColorYellow : GColorWhite);
      #else
      graphics_context_set_text_color(ctx, sel ? GColorWhite : GColorLightGray);
      #endif
      graphics_draw_text(ctx, "Play Skull", f_md,
        GRect(icon_x + 10, oy + 1, w/2, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      oy += 28;
      opt_count++;
    }
    // Bet (available after everyone played 1, or forced if hand empty)
    if(s_all_played_one || player_hand_count(pi) == 0) {
      bool sel = (s_cursor == opt_count);
      if(sel) {
        #ifdef PBL_COLOR
        graphics_context_set_fill_color(ctx, GColorFromHEX(0x003300));
        graphics_fill_rect(ctx, GRect(pad+20, oy, w-pad*2-40, 24), 4, GCornersAll);
        #endif
      }
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, sel ? GColorYellow : GColorGreen);
      #else
      graphics_context_set_text_color(ctx, sel ? GColorWhite : GColorLightGray);
      #endif
      graphics_draw_text(ctx, "Start Bet!", f_md,
        GRect(0, oy + 1, w, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      opt_count++;
    }
  }

  // ======== BET ========
  else if(s_state == ST_BET) {
    int pi = cp();
    // Table at top
    int ty = PBL_IF_ROUND_ELSE(28, 8);
    draw_table(ctx, w, h, ty, s_cur_idx, false);

    int tbl_rows = (s_num_players + 1) / 2;
    int by = ty + tbl_rows * 32 + 4;

    // Whose turn
    draw_token(ctx, w/2, by + 10, s_players[pi].icon, false);

    int oy = by + 24;
    int max_bet = total_on_table();

    if(s_is_first_bettor) {
      // First bettor: pick a number (1 to max), no pass
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, GColorYellow);
      #else
      graphics_context_set_text_color(ctx, GColorWhite);
      #endif
      char pick_str[20];
      snprintf(pick_str, sizeof(pick_str), "Bet: %d", s_bet_pick);
      graphics_draw_text(ctx, pick_str, f_lg,
        GRect(0, oy, w, 34),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      graphics_context_set_text_color(ctx, GColorLightGray);
      char range[20];
      snprintf(range, sizeof(range), "UP/DOWN: 1-%d", max_bet);
      graphics_draw_text(ctx, range, f_sm,
        GRect(0, oy + 32, w, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    } else {
      // Subsequent bettor: current bet shown, pick higher or pass
      graphics_context_set_text_color(ctx, GColorLightGray);
      char cur[16]; snprintf(cur, sizeof(cur), "Current: %d", s_current_bet);
      graphics_draw_text(ctx, cur, f_sm,
        GRect(0, oy, w, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

      if(s_cursor == 0) {
        // Raise selected
        #ifdef PBL_COLOR
        graphics_context_set_text_color(ctx, GColorYellow);
        #else
        graphics_context_set_text_color(ctx, GColorWhite);
        #endif
        char pick_str[20];
        snprintf(pick_str, sizeof(pick_str), "> Bet: %d", s_bet_pick);
        graphics_draw_text(ctx, pick_str, f_md,
          GRect(0, oy + 18, w, 22),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        #ifdef PBL_COLOR
        graphics_context_set_text_color(ctx, GColorLightGray);
        #endif
        graphics_draw_text(ctx, "  Pass", f_sm,
          GRect(0, oy + 40, w, 18),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      } else {
        // Pass selected
        graphics_context_set_text_color(ctx, GColorLightGray);
        char pick_str[20];
        snprintf(pick_str, sizeof(pick_str), "  Bet: %d", s_bet_pick);
        graphics_draw_text(ctx, pick_str, f_sm,
          GRect(0, oy + 18, w, 18),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        #ifdef PBL_COLOR
        graphics_context_set_text_color(ctx, GColorYellow);
        #else
        graphics_context_set_text_color(ctx, GColorWhite);
        #endif
        graphics_draw_text(ctx, "> Pass", f_md,
          GRect(0, oy + 38, w, 22),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      }
    }
  }

  // ======== REVEAL OWN ========
  else if(s_state == ST_REVEAL_OWN) {
    int pi = cp();
    int iy = PBL_IF_ROUND_ELSE(32, 14);
    draw_token(ctx, w/2, iy, s_players[pi].icon, true);
    graphics_context_set_text_color(ctx, GColorWhite);
    char info[24];
    snprintf(info, sizeof(info), "Revealing... %d/%d", s_roses_found, s_current_bet);
    graphics_draw_text(ctx, info, f_sm,
      GRect(0, iy + 20, w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "Your cards first!", f_md,
      GRect(0, iy + 40, w, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "SELECT to flip", f_sm,
      GRect(0, h-PBL_IF_ROUND_ELSE(28,18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== REVEAL PICK ========
  else if(s_state == ST_REVEAL_PICK) {
    int ty = PBL_IF_ROUND_ELSE(28, 8);
    draw_table(ctx, w, h, ty, s_cursor, true);

    int rows = (s_num_players + 1) / 2;
    int by = ty + rows * 32 + 4;
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorYellow);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    char info[24];
    snprintf(info, sizeof(info), "Roses: %d / %d", s_roses_found, s_current_bet);
    graphics_draw_text(ctx, info, f_md,
      GRect(0, by, w, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "Pick a player to flip", f_sm,
      GRect(0, by + 24, w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== FLIP ========
  else if(s_state == ST_FLIP) {
    int cy = h/2 - 30;
    draw_token(ctx, w/2, cy - 10, s_players[s_reveal_target].icon, true);
    if(s_flip_card == CARD_ROSE) {
      draw_rose(ctx, w/2, cy + 30);
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, GColorGreen);
      #else
      graphics_context_set_text_color(ctx, GColorWhite);
      #endif
      graphics_draw_text(ctx, "Rose!", f_lg,
        GRect(0, cy + 44, w, 34),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    } else {
      draw_skull_icon(ctx, w/2, cy + 30);
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, GColorRed);
      #else
      graphics_context_set_text_color(ctx, GColorWhite);
      #endif
      graphics_draw_text(ctx, "SKULL!", f_lg,
        GRect(0, cy + 44, w, 34),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "SELECT", f_sm,
      GRect(0, h-PBL_IF_ROUND_ELSE(28,18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== SKULL FOUND ========
  else if(s_state == ST_SKULL) {
    int cy = h/2 - 40;
    draw_skull_icon(ctx, w/2, cy);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorRed);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, "SKULL!", f_lg,
      GRect(0, cy + 16, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorWhite);
    int bettor = s_order[s_bettor_idx];
    draw_token(ctx, w/2, cy + 60, s_players[bettor].icon, false);
    graphics_draw_text(ctx, "loses a card!", f_sm,
      GRect(0, cy + 72, w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    if(s_players[bettor].eliminated) {
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, GColorRed);
      #endif
      graphics_draw_text(ctx, "ELIMINATED!", f_md,
        GRect(0, cy + 90, w, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "SELECT: next round", f_sm,
      GRect(0, h-PBL_IF_ROUND_ELSE(28,18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== WIN ROUND ========
  else if(s_state == ST_WIN_ROUND) {
    int bettor = s_order[s_bettor_idx];
    int cy = h/2 - 30;
    draw_token(ctx, w/2, cy, s_players[bettor].icon, true);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorYellow);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    char pts[16];
    snprintf(pts, sizeof(pts), "%d point%s!",
      s_players[bettor].points, s_players[bettor].points > 1 ? "s" : "");
    graphics_draw_text(ctx, "Round Won!", f_lg,
      GRect(0, cy + 22, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, pts, f_md,
      GRect(0, cy + 56, w, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "SELECT", f_sm,
      GRect(0, h-PBL_IF_ROUND_ELSE(28,18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // ======== GAME OVER ========
  else if(s_state == ST_GAMEOVER) {
    int bettor = s_order[s_bettor_idx];
    int cy = h/2 - 40;
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorYellow);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, "WINNER!", f_lg,
      GRect(0, cy, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    draw_token(ctx, w/2, cy + 48, s_players[bettor].icon, true);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, tok_color(s_players[bettor].icon));
    #endif
    graphics_draw_text(ctx, s_tok_name[s_players[bettor].icon], f_md,
      GRect(0, cy + 66, w, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "SELECT: new game", f_sm,
      GRect(0, h-PBL_IF_ROUND_ELSE(28,18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// ============================================================================
// PLAY OPTION COUNT (for cursor bounds)
// ============================================================================
static int play_option_count(void) {
  int pi = cp();
  Player *p = &s_players[pi];
  int c = 0;
  if(p->hand_roses > 0) c++;
  if(p->hand_skull) c++;
  if(s_all_played_one || player_hand_count(pi) == 0) c++;
  return c;
}

static int play_option_to_action(int opt) {
  // Returns: 0=rose, 1=skull, 2=bet
  int pi = cp();
  Player *p = &s_players[pi];
  int idx = 0;
  if(p->hand_roses > 0) { if(opt == idx) return 0; idx++; }
  if(p->hand_skull) { if(opt == idx) return 1; idx++; }
  return 2; // bet
}

// ============================================================================
// BUTTONS
// ============================================================================
static void select_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP) {
    s_num_players = s_setup_cursor + 3;
    shuffle_order();
    s_state = ST_ORDER;
  }
  else if(s_state == ST_ORDER) {
    init_players();
    s_round_starter = 0;
    start_round();
    s_state = ST_PASS;
  }
  else if(s_state == ST_PASS) {
    int pi = cp();
    // If hand empty, forced to bet
    if(player_hand_count(pi) == 0) {
      s_current_bet = 0;
      s_bettor_idx = s_cur_idx;
      s_is_first_bettor = true;
      s_in_bet_phase = true;
      s_bet_pick = 1;
      for(int i = 0; i < MAX_PLAYERS; i++) s_bet_passed[i] = false;
      s_cursor = 0;
      s_state = ST_BET;
    } else {
      s_cursor = 0;
      s_state = ST_PLAY;
    }
  }
  else if(s_state == ST_PLAY) {
    int pi = cp();
    Player *p = &s_players[pi];
    int action = play_option_to_action(s_cursor);

    if(action == 0) {
      // Play rose
      p->stack[p->stack_count++] = CARD_ROSE;
      p->hand_roses--;
    } else if(action == 1) {
      // Play skull
      p->stack[p->stack_count++] = CARD_SKULL;
      p->hand_skull = false;
    } else {
      // Start bet — first bettor picks a number
      s_current_bet = 0;
      s_bettor_idx = s_cur_idx;
      s_is_first_bettor = true;
      s_in_bet_phase = true;
      s_bet_pick = 1;
      for(int i = 0; i < MAX_PLAYERS; i++) s_bet_passed[i] = false;
      s_cursor = 0;
      s_state = ST_BET;
      if(s_canvas) layer_mark_dirty(s_canvas);
      return;
    }

    // Advance to next player
    s_cur_idx = next_active(s_cur_idx);
    s_all_played_one = everyone_played_one();
    s_cursor = 0;
    s_state = ST_PASS;
  }
  else if(s_state == ST_BET) {
    bool did_bet = false;
    if(s_is_first_bettor) {
      // First bettor confirms their pick
      s_current_bet = s_bet_pick;
      s_bettor_idx = s_cur_idx;
      s_is_first_bettor = false;
      did_bet = true;
    } else if(s_cursor == 0) {
      // Raise to s_bet_pick
      s_current_bet = s_bet_pick;
      s_bettor_idx = s_cur_idx;
      for(int i = 0; i < MAX_PLAYERS; i++) s_bet_passed[i] = false;
      did_bet = true;
    } else {
      // Pass
      s_bet_passed[s_cur_idx] = true;
    }

    // Check if bet maxed out
    if(did_bet && s_current_bet >= total_on_table()) {
      s_cur_idx = s_bettor_idx;
      s_roses_found = 0;
      Player *bp = &s_players[cp()];
      s_state = (bp->stack_count > 0) ? ST_REVEAL_OWN : ST_REVEAL_PICK;
      s_cursor = 0;
      if(s_canvas) layer_mark_dirty(s_canvas);
      return;
    }

    // Advance to next active non-passed player
    do {
      s_cur_idx = next_active(s_cur_idx);
    } while(s_bet_passed[s_cur_idx] && s_cur_idx != s_bettor_idx);

    // If only bettor remains, they reveal
    if(s_cur_idx == s_bettor_idx) {
      s_roses_found = 0;
      Player *bp = &s_players[cp()];
      s_state = (bp->stack_count > 0) ? ST_REVEAL_OWN : ST_REVEAL_PICK;
      s_cursor = 0;
    } else {
      // Set up next player's bet UI
      s_bet_pick = s_current_bet + 1;
      s_cursor = 1; // default to Pass
    }
  }
  else if(s_state == ST_REVEAL_OWN) {
    // Flip own top card
    int pi = cp();
    Player *p = &s_players[pi];
    int top = p->stack_count - 1 - p->revealed;
    s_flip_card = p->stack[top];
    s_reveal_target = pi;
    p->revealed++;
    if(s_flip_card == CARD_ROSE) s_roses_found++;
    s_state = ST_FLIP;
  }
  else if(s_state == ST_FLIP) {
    if(s_flip_card == CARD_SKULL) {
      // Skull found
      s_skull_owner = s_reveal_target;
      lose_random_card(s_order[s_bettor_idx]);
      s_state = ST_SKULL;
      vibes_long_pulse();
    } else if(s_roses_found >= s_current_bet) {
      // Won!
      int bettor = s_order[s_bettor_idx];
      s_players[bettor].points++;
      if(s_players[bettor].points >= 2) {
        s_state = ST_GAMEOVER;
      } else {
        s_state = ST_WIN_ROUND;
      }
      vibes_short_pulse();
    } else {
      // Continue revealing
      int pi = cp();
      Player *p = &s_players[pi];
      if(p->revealed < p->stack_count) {
        // More own cards to flip
        s_state = ST_REVEAL_OWN;
      } else {
        // Pick other players
        // Find first valid target
        s_cursor = 0;
        for(int i = 0; i < s_num_players; i++) {
          int ti = s_order[i];
          if(!s_players[ti].eliminated && i != s_bettor_idx &&
             s_players[ti].revealed < s_players[ti].stack_count) {
            s_cursor = i;
            break;
          }
        }
        s_state = ST_REVEAL_PICK;
      }
    }
  }
  else if(s_state == ST_REVEAL_PICK) {
    // Flip selected player's top card
    int ti = s_order[s_cursor];
    Player *p = &s_players[ti];
    if(!p->eliminated && p->revealed < p->stack_count && s_cursor != s_bettor_idx) {
      int top = p->stack_count - 1 - p->revealed;
      s_flip_card = p->stack[top];
      s_reveal_target = ti;
      p->revealed++;
      if(s_flip_card == CARD_ROSE) s_roses_found++;
      s_state = ST_FLIP;
    }
  }
  else if(s_state == ST_SKULL) {
    // Next round
    s_round_starter = next_active(s_bettor_idx);
    // Check if only 1 player left
    if(active_count() <= 1) {
      // Last player standing wins
      for(int i = 0; i < s_num_players; i++)
        if(!s_players[s_order[i]].eliminated) { s_bettor_idx = i; break; }
      s_state = ST_GAMEOVER;
    } else {
      start_round();
      s_state = ST_PASS;
    }
  }
  else if(s_state == ST_WIN_ROUND) {
    s_round_starter = s_bettor_idx;
    start_round();
    s_state = ST_PASS;
  }
  else if(s_state == ST_GAMEOVER) {
    s_state = ST_SETUP;
    s_setup_cursor = s_num_players - 3;
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP) {
    s_setup_cursor = (s_setup_cursor + 3) % 4;
  } else if(s_state == ST_PLAY) {
    int max = play_option_count();
    s_cursor = (s_cursor + max - 1) % max;
  } else if(s_state == ST_BET) {
    int max_bet = total_on_table();
    if(s_is_first_bettor) {
      // Number picker only
      if(s_bet_pick > 1) s_bet_pick--;
    } else if(s_cursor == 0) {
      // On bet: decrease number or switch to pass
      if(s_bet_pick > s_current_bet + 1) s_bet_pick--;
      else s_cursor = 1; // switch to pass
    } else {
      // On pass: switch to bet
      s_cursor = 0;
    }
  } else if(s_state == ST_REVEAL_PICK) {
    int start = s_cursor;
    do {
      s_cursor = (s_cursor + s_num_players - 1) % s_num_players;
      int ti = s_order[s_cursor];
      if(!s_players[ti].eliminated && s_cursor != s_bettor_idx &&
         s_players[ti].revealed < s_players[ti].stack_count) break;
    } while(s_cursor != start);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP) {
    s_setup_cursor = (s_setup_cursor + 1) % 4;
  } else if(s_state == ST_PLAY) {
    int max = play_option_count();
    s_cursor = (s_cursor + 1) % max;
  } else if(s_state == ST_BET) {
    int max_bet = total_on_table();
    if(s_is_first_bettor) {
      if(s_bet_pick < max_bet) s_bet_pick++;
    } else if(s_cursor == 0) {
      // On bet: increase number
      if(s_bet_pick < max_bet) s_bet_pick++;
    } else {
      // On pass: switch to bet
      s_cursor = 0;
    }
  } else if(s_state == ST_REVEAL_PICK) {
    int start = s_cursor;
    do {
      s_cursor = (s_cursor + 1) % s_num_players;
      int ti = s_order[s_cursor];
      if(!s_players[ti].eliminated && s_cursor != s_bettor_idx &&
         s_players[ti].revealed < s_players[ti].stack_count) break;
    } while(s_cursor != start);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void back_click(ClickRecognizerRef ref, void *ctx) {
  if(s_state == ST_SETUP || s_state == ST_GAMEOVER)
    window_stack_pop(true);
  else {
    s_state = ST_SETUP; s_setup_cursor = 0;
    if(s_canvas) layer_mark_dirty(s_canvas);
  }
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ============================================================================
// LIFECYCLE
// ============================================================================
static void win_load(Window *w) {
  Layer *wl = window_get_root_layer(w);
  GRect b = layer_get_bounds(wl);
  s_canvas = layer_create(b);
  layer_set_update_proc(s_canvas, canvas_proc);
  layer_add_child(wl, s_canvas);
  window_set_click_config_provider(w, click_config);
}

static void win_unload(Window *w) {
  if(s_canvas) { layer_destroy(s_canvas); s_canvas = NULL; }
}

static void init(void) {
  srand(time(NULL));
  s_icon_font_20 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ICON_FONT_20));
  s_icon_font_14 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ICON_FONT_14));
  s_win = window_create();
  window_set_background_color(s_win, GColorBlack);
  window_set_window_handlers(s_win, (WindowHandlers){.load=win_load,.unload=win_unload});
  window_stack_push(s_win, true);
}

static void deinit(void) {
  window_destroy(s_win);
  fonts_unload_custom_font(s_icon_font_20);
  fonts_unload_custom_font(s_icon_font_14);
}

int main(void) { init(); app_event_loop(); deinit(); return 0; }
