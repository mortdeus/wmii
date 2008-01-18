/* Copyright ©2006-2008 Kris Maglione <fbsdaemon@gmail.com>
 * See LICENSE file for license details.
 */
#include "dat.h"
#include <assert.h>
#include <ctype.h>
#include "fns.h"

static char* msg_debug(IxpMsg*);
static char* msg_selectframe(Frame*, IxpMsg*, int);
static char* msg_sendframe(Frame*, int, bool);

static char
	Ebadcmd[] = "bad command",
	Ebadvalue[] = "bad value",
	Ebadusage[] = "bad usage";

/* Edit |sort   Edit s/"([^"]+)"/L\1/g   Edit |tr 'a-z' 'A-Z' */
enum {
	LFULLSCREEN,
	LURGENT,
	LBORDER,
	LCLIENT,
	LCOLMODE,
	LDEBUG,
	LDOWN,
	LEXEC,
	LFOCUSCOLORS,
	LFONT,
	LGRABMOD,
	LKILL,
	LLEFT,
	LNORMCOLORS,
	LOFF,
	LON,
	LQUIT,
	LRIGHT,
	LSELCOLORS,
	LSELECT,
	LSEND,
	LSLAY,
	LSWAP,
	LTOGGLE,
	LUP,
	LVIEW,
	LTILDE,
};
char *symtab[] = {
	"Fullscreen",
	"Urgent",
	"border",
	"client",
	"colmode",
	"debug",
	"down",
	"exec",
	"focuscolors",
	"font",
	"grabmod",
	"kill",
	"left",
	"normcolors",
	"off",
	"on",
	"quit",
	"right",
	"selcolors",
	"select",
	"send",
	"slay",
	"swap",
	"toggle",
	"up",
	"view",
	"~",
};

char *debugtab[] = {
	"event",
	"ewmh",
	"focus",
	"generic",
};

/* Edit ,y/^[a-zA-Z].*\n.* {\n/d
 * Edit s/^([a-zA-Z].*)\n(.*) {\n/\1 \2;\n/
 * Edit ,x/^static.*\n/d
 */

static int
_bsearch(char *s, char **tab, int ntab) {
	int i, n, m, cmp;

	if(s == nil)
		return -1;

	n = ntab;
	i = 0;
	while(n) {
		m = n/2;
		cmp = strcmp(s, tab[i+m]);
		if(cmp == 0)
			return i+m;
		if(cmp < 0 || m == 0)
			n = m;
		else {
			i += m;
			n = n-m;
		}
	}
	return -1;
}

static int
getsym(char *s) {
	return _bsearch(s, symtab, nelem(symtab));
}

static int
getdebug(char *s) {
	return _bsearch(s, debugtab, nelem(debugtab));
}

static int
gettoggle(IxpMsg *m) {
	switch(getsym(msg_getword(m))) {
	case LON:
		return On;
	case LOFF:
		return Off;
	case LTOGGLE:
		return Toggle;
	default:
		return -1;
	}
}

static void
eatrunes(IxpMsg *m, int (*p)(Rune), int val) {
	Rune r;
	int n;

	while(m->pos < m->end) {
		n = chartorune(&r, m->pos);
		if(p(r) != val)
			break;
		m->pos += n;
	}
	if(m->pos > m->end)
		m->pos = m->end;
}

char*
msg_getword(IxpMsg *m) {
	char *ret;
	Rune r;
	int n;

	eatrunes(m, isspacerune, 1);
	ret = m->pos;
	eatrunes(m, isspacerune, 0);
	n = chartorune(&r, m->pos);
	*m->pos = '\0';
	m->pos += n;
	eatrunes(m, isspacerune, 1);

	if(ret == m->end)
		return nil;
	return ret;
}

#define strbcmp(str, const) (strncmp((str), (const), sizeof(const)-1))	
static int
getbase(const char **s) {
	const char *p;

	p = *s;
	if(!strbcmp(p, "0x")) {
		*s += 2;
		return 16;
	}
	if(isdigit(p[0])) {
		if(p[1] == 'r') {
			*s += 2;
			return p[0] - '0';
		}
		if(isdigit(p[1]) && p[2] == 'r') {
			*s += 3;
			return 10*(p[0]-'0') + (p[1]-'0');
		}
	}
	if(p[0] == '0') {
		*s += 1;
		return 8;
	}
	return 10;
}

int
getlong(const char *s, long *ret) {
	const char *end;
	char *rend;
	int base;

	end = s+strlen(s);
	base = getbase(&s);

	*ret = strtol(s, &rend, base);
	return (end == rend);
}

int
getulong(const char *s, ulong *ret) {
	const char *end;
	char *rend;
	int base;

	end = s+strlen(s);
	base = getbase(&s);

	*ret = strtoul(s, &rend, base);
	return (end == rend);
}

static Client*
strclient(View *v, char *s) {
	ulong id;

	if(!strcmp(s, "sel"))
		return view_selclient(v);
	if(getulong(s, &id))
		return win2client(id);

	return nil;
}

Area*
strarea(View *v, const char *s) {
	Area *a;
	long i;

	if(!strcmp(s, "sel"))
		return v->sel;
	if(!strcmp(s, "~"))
		return v->area;
	if(!getlong(s, &i) || i == 0)
		return nil;

	if(i > 0) {
		for(a = v->area; a; a = a->next)
			if(i-- == 0) break;
	}
	else {
		for(a = v->area; a->next; a = a->next)
			;
		for(; a != v->area; a = a->prev)
			if(++i == 0) break;
		if(a == v->area)
			a = nil;
	}
	return a;
}

char*
message_client(Client *c, IxpMsg *m) {
	char *s;
	int i;

	s = msg_getword(m);

	switch(getsym(s)) {
	case LFULLSCREEN:
		i = gettoggle(m);
		if(i == -1)
			return Ebadusage;
		fullscreen(c, i);
		break;
	case LKILL:
		client_kill(c, true);
		break;
	case LSLAY:
		client_kill(c, false);
		break;
	case LURGENT:
		i = gettoggle(m);
		if(i == -1)
			return Ebadusage;
		client_seturgent(c, i, True);
		break;
	default:
		return Ebadcmd;
	}
	return nil;
}

char*
message_root(void *p, IxpMsg *m) {
	Font *fn;
	char *s, *ret;
	ulong n;

	USED(p);
	ret = nil;
	s = msg_getword(m);

	switch(getsym(s)) {
	case LBORDER:
		if(!getulong(msg_getword(m), &n))
			return Ebadvalue;
		def.border = n;
		view_focus(screen, screen->sel);
		break;
	case LDEBUG:
		ret = msg_debug(m);
		break;
	case LEXEC:
		execstr = smprint("exec %s", m->pos);
		srv.running = 0;
		break;
	case LFOCUSCOLORS:
		ret = msg_parsecolors(m, &def.focuscolor);
		view_focus(screen, screen->sel);
		break;
	case LFONT:
		fn = loadfont(m->pos);
		if(fn) {
			freefont(def.font);
			def.font = fn;
			bar_resize(screen);
		}else
			ret = "can't load font";
		view_focus(screen, screen->sel);
		break;
	case LGRABMOD:
		s = msg_getword(m);
		n = str2modmask(s);

		if((n & (Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask)) == 0)
			return Ebadvalue;

		utflcpy(def.grabmod, s, sizeof(def.grabmod));
		def.mod = n;
		break;
	case LNORMCOLORS:
		ret = msg_parsecolors(m, &def.normcolor);
		view_focus(screen, screen->sel);
		break;
	case LSELCOLORS:
		fprint(2, "%s: warning: selcolors have been removed\n", argv0);
		return Ebadcmd;
	case LVIEW:
		view_select(m->pos);
		break;
	case LQUIT:
		srv.running = 0;
		break;
	default:
		return Ebadcmd;
	}
	return ret;
}

char*
message_view(View *v, IxpMsg *m) {
	Area *a;
	char *s;
	int i;

	s = msg_getword(m);

	switch(getsym(s)) {
	case LCOLMODE:
		s = msg_getword(m);
		a = strarea(v, s);
		if(a == nil || a->floating)
			return Ebadvalue;

		s = msg_getword(m);
		i = str2colmode(s);
		if(i == -1)
			return Ebadvalue;

		a->mode = i;
		column_arrange(a, True);
		view_restack(v);

		if(v == screen->sel)
			view_focus(screen, v);
		frame_draw_all();
		return nil;
	case LSELECT:
		return msg_selectarea(v->sel, m);
	case LSEND:
		return msg_sendclient(v, m, 0);
	case LSWAP:
		return msg_sendclient(v, m, 1);
	default:
		return Ebadcmd;
	}
	/* not reached */
}

char *
parse_colors(IxpMsg *m, CTuple *col) {
	static char Ebad[] = "bad color string";
	Rune r;
	char c, *p;
	int i, j;

	/* '#%6x #%6x #%6x' */
	p = m->pos;
	for(i = 0; i < 3 && p < m->end; i++) {
		if(*p++ != '#')
			return Ebad;
		for(j = 0; j < 6; j++)
			if(p >= m->end || !isxdigit(*p++))
				return Ebad;

		chartorune(&r, p);
		if(i < 2) {
			if(r != ' ')
				return Ebad;
			p++;
		}else if(*p != '\0' && !isspacerune(r))
			return Ebad;
	}

	c = *p;
	*p = '\0';
	loadcolor(col, m->pos);
	*p = c;

	m->pos = p;
	eatrunes(m, isspacerune, 1);
	return nil;
}

char*
msg_selectarea(Area *a, IxpMsg *m) {
	Frame *f;
	Area *ap;
	View *v;
	char *s;
	ulong i;
	int sym;

	v = a->view;
	s = msg_getword(m);
	sym = getsym(s);

	switch(sym) {
	case LTOGGLE:
		if(!a->floating)
			ap = v->area;
		else if(v->revert && v->revert != a)
			ap = v->revert;
		else
			ap = v->area->next;
		break;
	case LUP:
	case LDOWN:
	case LCLIENT:
		return msg_selectframe(a->sel, m, sym);
	case LLEFT:
		if(a->floating)
			return Ebadvalue;
		for(ap=v->area->next; ap->next; ap=ap->next)
			if(ap->next == a) break;
		break;
	case LRIGHT:
		if(a->floating)
			return Ebadvalue;
		ap = a->next;
		if(ap == nil)
			ap = v->area->next;
		break;
	case LTILDE:
		ap = v->area;
		break;
	default:
		if(!strcmp(s, "sel"))
			ap = v->sel;
		else {
			if(!getulong(s, &i) || i == 0)
				return Ebadvalue;
			for(ap=v->area->next; ap; ap=ap->next)
				if(--i == 0) break;
			if(i != 0)
				return Ebadvalue;
		}
		if((s = msg_getword(m))) {
			if(!getulong(s, &i))
				return Ebadvalue;
			for(f = ap->frame; f; f = f->anext)
				if(--i == 0) break;
			if(i != 0)
				return Ebadvalue;
			frame_focus(f);
			return nil;
		}
	}

	area_focus(ap);
	return nil;
}

static char*
msg_selectframe(Frame *f, IxpMsg *m, int sym) {
	Frame *fp;
	Client *c;
	Area *a;
	char *s;
	ulong i;

	if(!f)
		return Ebadvalue;
	a = f->area;

	SET(fp);
	switch(sym) {
	case LUP:
		for(fp=a->frame; fp->anext; fp=fp->anext)
			if(fp->anext == f) break;
		break;
	case LDOWN:
		fp = f->anext;
		if(fp == nil)
			fp = a->frame;
		break;
	case LCLIENT:
		s = msg_getword(m);
		if(s == nil || !getulong(s, &i))
			return "usage: select client <client>";
		c = win2client(i);
		if(c == nil)
			return "unknown client";
		fp = client_viewframe(c, f->view);
		break;
	default:
		die("can't get here");
	}

	if(fp == nil)
		return "invalid selection";

	frame_focus(fp);
	frame_restack(fp, nil);
	if(f->view == screen->sel)
		view_restack(f->view);
	return nil;
}

char*
msg_sendclient(View *v, IxpMsg *m, bool swap) {
	Area *to, *a;
	Frame *f;
	Client *c;
	char *s;
	ulong i;
	int sym;

	s = msg_getword(m);

	c = strclient(v, s);
	if(c == nil)
		return Ebadvalue;

	f = client_viewframe(c, v);
	if(f == nil)
		return Ebadvalue;

	a = f->area;
	to = nil;

	s = msg_getword(m);
	sym = getsym(s);

	switch(sym) {
	case LUP:
	case LDOWN:
		return msg_sendframe(f, sym, swap);
	case LLEFT:
		if(a->floating)
			return Ebadvalue;

		if(a->prev != v->area)
			to = a->prev;
		a = v->area;
		break;
	case LRIGHT:
		if(a->floating)
			return Ebadvalue;

		to = a->next;
		break;
	case LTOGGLE:
		if(!a->floating)
			to = v->area;
		else if(c->revert && !c->revert->floating)
			to = c->revert;
		else
			to = v->area->next;
		break;
	default:
		if(!getulong(s, &i) || i == 0)
			return Ebadvalue;

		for(to=v->area; to; to=to->next)
			if(!i--) break;
		break;
	}

	if(!to && !swap && (f->anext || f != f->area->frame))
		to = column_new(v, a, 0);

	if(!to)
		return Ebadvalue;

	if(!swap)
		area_moveto(to, f);
	else if(to->sel)
		frame_swap(f, to->sel);
	else
		return Ebadvalue;

	flushenterevents();
	frame_focus(f);
	view_arrange(v);
	view_update_all();
	return nil;
}

static char*
msg_sendframe(Frame *f, int sym, bool swap) {
	Frame *fp;

	SET(fp);
	switch(sym) {
	case LUP:
		fp = f->aprev;
		if(!fp)
			return Ebadvalue;
		fp = fp->aprev;
		break;
	case LDOWN:
		fp = f->anext;
		if(!fp)
			return Ebadvalue;
		break;
	default:
		die("can't get here");
	}

	if(swap) {
		if(!fp)
			return Ebadvalue;
		frame_swap(f, fp);
	}else {
		frame_remove(f);
		frame_insert(fp, f);
	}

	view_arrange(f->view);

	flushenterevents();
	frame_focus(f);
	view_update_all();
	return nil;
}
