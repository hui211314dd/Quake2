/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include "client.h"
#include "qmenu.h"

#define CHAR_WIDTH	8
#define CHAR_HEIGHT	8

static void	 Action_DoEnter (menuAction_t *a);
static void	 Action_Draw (menuAction_t *a);
static void  Menu_DrawStatusBar (const char *string);
static void	 MenuList_Draw (menuList_t *l);
static void	 Separator_Draw (menuSeparator_t *s);
static void	 Slider_DoSlide (menuSlider_t *s, int dir);
static void	 Slider_Draw (menuSlider_t *s);
static void	 SpinControl_Draw (menuList_t *s);
static void	 SpinControl_DoSlide (menuList_t *s, int dir);
static void	 SpinControl2_Draw (menuList2_t *s);
static void	 SpinControl2_DoSlide (menuList2_t *s, int dir);

#define RCOLUMN_OFFSET  16
#define LCOLUMN_OFFSET -16


#define VID_WIDTH		viddef.width
#define VID_HEIGHT		viddef.height


static menuCommon_t *GetItem (menuFramework_t *menu, int index)
{
	int		i;
	menuCommon_t *item;

	for (i = 0, item = menu->itemList; i < index && item; i++, item = item->next) ;

	if (!item)
		Com_FatalError ("GetMenuItem: item index %d is out of bounds %d\n", index, menu->nitems);

	return item;
}

menuCommon_t *Menu_ItemAtCursor (menuFramework_t *m)
{
	if (m->cursor < 0 || m->cursor >= m->nitems)
		return NULL;

	return GetItem (m, m->cursor);
}

static void Action_DoEnter (menuAction_t *a)
{
	if (a->generic.callback) a->generic.callback (a);
}

static void Action_Draw (menuAction_t *a)
{
	if (a->generic.flags & QMF_LEFT_JUSTIFY)
		DrawString (a->generic.x + a->generic.parent->x + LCOLUMN_OFFSET, a->generic.y + a->generic.parent->y,
			va("%s%s", a->generic.flags & QMF_GRAYED ? S_GREEN : "", a->generic.name));
	else
		Menu_DrawStringR2L (a->generic.x + a->generic.parent->x + LCOLUMN_OFFSET, a->generic.y + a->generic.parent->y,
			va("%s%s", a->generic.flags & QMF_GRAYED ? S_GREEN : "", a->generic.name));
	if (a->generic.ownerdraw)
		a->generic.ownerdraw (a);
}

static bool Field_DoEnter (menuField_t *f)
{
	if (f->generic.callback)
	{
		f->generic.callback( f );
		return true;
	}
	return false;
}

static void Field_Draw (menuField_t *f)
{
	int		i;
	char	tempbuffer[128];

	// draw caption
	if (f->generic.name)
		Menu_DrawStringR2L (f->generic.x + f->generic.parent->x + LCOLUMN_OFFSET, f->generic.y + f->generic.parent->y,
			va(S_GREEN"%s", f->generic.name));

	// draw border
	re.DrawChar (f->generic.x + f->generic.parent->x + CHAR_WIDTH * 2, f->generic.y + f->generic.parent->y - CHAR_HEIGHT/2, 18);
	re.DrawChar (f->generic.x + f->generic.parent->x + CHAR_WIDTH * 2, f->generic.y + f->generic.parent->y + CHAR_HEIGHT/2, 24);
	re.DrawChar (f->generic.x + f->generic.parent->x + (f->visible_length + 3) * CHAR_WIDTH, f->generic.y + f->generic.parent->y - CHAR_HEIGHT/2, 20);
	re.DrawChar (f->generic.x + f->generic.parent->x + (f->visible_length + 3) * CHAR_WIDTH, f->generic.y + f->generic.parent->y + CHAR_HEIGHT/2, 26);
	for (i = 0; i < f->visible_length; i++)
	{
		re.DrawChar (f->generic.x + f->generic.parent->x + (i + 3) * CHAR_WIDTH, f->generic.y + f->generic.parent->y - CHAR_HEIGHT/2, 19);
		re.DrawChar (f->generic.x + f->generic.parent->x + (i + 3) * CHAR_WIDTH, f->generic.y + f->generic.parent->y + CHAR_HEIGHT/2, 25);
	}

	// perform string drawing without text coloring
	appStrncpyz (tempbuffer, f->buffer + f->visible_offset, max(f->visible_length, sizeof(tempbuffer)));
	const char *s = tempbuffer;
	int x = f->generic.x + f->generic.parent->x + 3 * CHAR_WIDTH;
	int y = f->generic.y + f->generic.parent->y;
	while (char c = *s++)
	{
		re.DrawChar (x, y, c);
		x += CHAR_WIDTH;
	}

	if (Menu_ItemAtCursor (f->generic.parent ) == (menuCommon_t*)f)
	{
		int offset;

		if (f->visible_offset)
			offset = f->visible_length;
		else
			offset = f->cursor;

		// show cursor
		re.DrawChar (f->generic.x + f->generic.parent->x + (offset + 3) * CHAR_WIDTH,
					 f->generic.y + f->generic.parent->y, ((Sys_Milliseconds() / 250) & 1) ? 11 : ' ');
	}
}

bool Field_Key (menuField_t *f, int key)
{
	switch (key)
	{
	case K_KP_SLASH:
		key = '/';
		break;
	case K_KP_MINUS:
		key = '-';
		break;
	case K_KP_PLUS:
		key = '+';
		break;
	case K_KP_HOME:
		key = '7';
		break;
	case K_KP_UPARROW:
		key = '8';
		break;
	case K_KP_PGUP:
		key = '9';
		break;
	case K_KP_LEFTARROW:
		key = '4';
		break;
	case K_KP_5:
		key = '5';
		break;
	case K_KP_RIGHTARROW:
		key = '6';
		break;
	case K_KP_END:
		key = '1';
		break;
	case K_KP_DOWNARROW:
		key = '2';
		break;
	case K_KP_PGDN:
		key = '3';
		break;
	case K_KP_INS:
		key = '0';
		break;
	case K_KP_DEL:
		key = '.';
		break;
	}

	switch (key)
	{
	case K_KP_LEFTARROW:
	case K_LEFTARROW:
	case K_BACKSPACE:
		if (f->cursor > 0)
		{
			memmove (&f->buffer[f->cursor-1], &f->buffer[f->cursor], strlen (&f->buffer[f->cursor]) + 1);
			f->cursor--;

			if (f->visible_offset)
			{
				f->visible_offset--;
			}
		}
		break;

	case K_KP_DEL:
	case K_DEL:
		memmove (&f->buffer[f->cursor], &f->buffer[f->cursor+1], strlen (&f->buffer[f->cursor+1]) + 1);
		break;

	case K_KP_ENTER:
	case K_ENTER:
	case K_ESCAPE:
	case K_TAB:
		return false;

	default:
		if (key < 32 || key >= 128) return false;
		if ((f->generic.flags & QMF_NUMBERSONLY) && (key < '0' || key > '9'))
			return false;

		if (f->cursor < f->length)
		{
			f->buffer[f->cursor++] = key;
			f->buffer[f->cursor] = 0;

			if (f->cursor > f->visible_length)
			{
				f->visible_offset++;
			}
		}
	}

	return true;
}

void Menu_AddItem (menuFramework_t *menu, void *item)
{
	menuCommon_t *last, *p, *c;
	int		i;

	guard(Menu_AddItem);
	// NOTE: menu without items will have nitems==0, but itemList will not be NULL !
	c = (menuCommon_t*) item;
	// find last item
	last = NULL;
	for (i = 0, p = menu->itemList; i < menu->nitems; i++, p = p->next)
	{
		if (p == c)					// item already present in list -- fatal (circular linked list)
			Com_FatalError ("double item \"%s\" in menu, index=%d, count=%d", c->name, i, menu->nitems);
		last = p;
	}
	if (last && last->next)			// last item (with index == menu->nitem) have next != NULL
		Com_FatalError ("invalid item list");
	// add to list
	if (i > 0)	last->next = c;		// append to list tail
	else		menu->itemList = c;	// first in list
	menu->nitems++;
	// setup new item
	c->parent = menu;
	c->next = NULL;
	unguard;
}


static bool Menu_CompletelyVisible (menuFramework_t *menu)
{
	int y0 = menu->itemList->y + menu->y;					// 1st item
	int y1 = GetItem (menu, menu->nitems - 1)->y + menu->y;	// last item
	if (y0 >= 0 && y1 < VID_HEIGHT - 10) return true;
	return false;
}


static bool Menu_ItemVisible (menuFramework_t *menu, int i)
{
	if (Menu_CompletelyVisible (menu))
		return true;

	if (menu->y > MENU_SCROLL_BORDER)
		return true;

	if (i < 0 || i >= menu->nitems)
		return false;
	int y = GetItem (menu, i)->y + menu->y;
	return (y >= MENU_SCROLL_BORDER && y <= VID_HEIGHT - MENU_SCROLL_BORDER);
}


static void Menu_MakeVisible (menuFramework_t *menu, int i)
{
	if (Menu_CompletelyVisible (menu)) return;

	int y0 = menu->itemList->y;						// 1st item
	int y1 = GetItem (menu, menu->nitems - 1)->y;	// last item
	if (y0 + menu->y > MENU_SCROLL_BORDER)
	{
		menu->y = MENU_SCROLL_BORDER - y0;
		return;
	}
	if (y1 + menu->y < VID_HEIGHT - MENU_SCROLL_BORDER)
	{
		menu->y = VID_HEIGHT - MENU_SCROLL_BORDER - y1;
		return;
	}

	i = bound (i, 0, menu->nitems-1);
	int y = GetItem (menu, i)->y + menu->y;

	if (y < MENU_SCROLL_BORDER)
		menu->y += MENU_SCROLL_BORDER - y;
	else if (y > VID_HEIGHT - MENU_SCROLL_BORDER)
		menu->y += VID_HEIGHT - MENU_SCROLL_BORDER - y;
}


/*
 * Menu_AdjustCursor
 *
 * This function takes the given menu, the direction, and attempts
 * to adjust the menu's cursor so that it's at the next available
 * slot.
 */
void Menu_AdjustCursor (menuFramework_t *m, int dir)
{
	menuCommon_t *citem;

	/* it's not in a valid spot, so crawl in the direction indicated until we
	 * find a valid spot
	 */
	if (dir == 1)
	{
		while (true)
		{
			citem = Menu_ItemAtCursor (m);
			if (citem)
				if (citem->type != MTYPE_SEPARATOR)
					break;
			m->cursor += dir;
			if (m->cursor >= m->nitems)
				// m->cursor = 0; -- will wrap cursor
				dir = -1;
		}
	}
	else
	{
		while (true)
		{
			citem = Menu_ItemAtCursor (m);
			if (citem)
				if (citem->type != MTYPE_SEPARATOR)
					break;
			m->cursor += dir;
			if (m->cursor < 0)
				// m->cursor = m->nitems - 1; -- will wrap cursor
				dir = 1;
		}
	}

	Menu_MakeVisible (m, m->cursor - MENU_SCROLL_AHEAD);
	Menu_MakeVisible (m, m->cursor + MENU_SCROLL_AHEAD);
}


void Menu_Center (menuFramework_t *menu)
{
	int height = GetItem (menu, menu->nitems-1)->y + 10;	// last item
	menu->y = (VID_HEIGHT - height) / 2;
	if (menu->y < MENU_SCROLL_BORDER && !Menu_CompletelyVisible (menu))
		menu->y = MENU_SCROLL_BORDER;
}


static void Menu_DrawDotsItem (menuCommon_t *item)
{
	int center = VID_WIDTH / 2;
	int y = item->y + item->parent->y;
	for (int x = center - 128; x < center + 128; x += CHAR_WIDTH)
		re.DrawChar (x, y, 0, C_RED);
}


void Menu_Draw (menuFramework_t *menu)
{
	int		i, vis;
	menuCommon_t *item;

	/*------- draw contents -------*/
	vis = -1;
	for (i = 0, item = menu->itemList; i < menu->nitems; i++, item = item->next)
	{
		if (Menu_ItemVisible (menu, i))
		{
			if (vis == -1)	// previous item vas invisible
			{
				vis = 0;
				if (i > 0)	// it is not a first item
				{
					Menu_DrawDotsItem (item);
					continue;
				}
			}
			else if (i < menu->nitems - 1 && !Menu_ItemVisible (menu, i + 1))
			{
				Menu_DrawDotsItem (item);
				break;	// no more visible items
			}
		}
		else
			continue;

		switch (item->type)
		{
		case MTYPE_FIELD:
			Field_Draw ((menuField_t*)item);
			break;
		case MTYPE_SLIDER:
			Slider_Draw ((menuSlider_t*)item);
			break;
		case MTYPE_LIST:
			MenuList_Draw ((menuList_t*)item);
			break;
		case MTYPE_SPINCONTROL:
			SpinControl_Draw ((menuList_t*)item);
			break;
		case MTYPE_SPINCONTROL2:
			SpinControl2_Draw ((menuList2_t*)item);
			break;
		case MTYPE_ACTION:
			Action_Draw ((menuAction_t*)item);
			break;
		case MTYPE_SEPARATOR:
			Separator_Draw ((menuSeparator_t*)item);
			break;
		}
	}

	item = Menu_ItemAtCursor (menu);

	if (item && item->cursordraw)
	{
		item->cursordraw (item);
	}
	else if (menu->cursordraw)
	{
		menu->cursordraw (menu);
	}
	else if (item && item->type != MTYPE_FIELD)
	{
		if (item->flags & QMF_LEFT_JUSTIFY)
		{
			re.DrawChar (menu->x + item->x - 24 + item->cursor_offset, menu->y + item->y, 12 + (Sys_Milliseconds()/250 & 1));
		}
		else
		{
			re.DrawChar (menu->x + item->cursor_offset, menu->y + item->y, 12 + (Sys_Milliseconds()/250 & 1));
		}
	}

	if (item)
	{
		if (item->statusbar)
			Menu_DrawStatusBar (item->statusbar);
		else
			Menu_DrawStatusBar (menu->statusbar);

	}
	else
		Menu_DrawStatusBar (menu->statusbar);
}

void Menu_DrawStatusBar (const char *string)
{
	if (string)
	{
		int		l, maxrow, maxcol, col;

		l = appCStrlen (string);
		maxrow = VID_HEIGHT / CHAR_HEIGHT;
		maxcol = VID_WIDTH / CHAR_WIDTH;
		col = maxcol / 2 - l / 2;

		re.DrawFill (0, VID_HEIGHT - CHAR_HEIGHT, VID_WIDTH, CHAR_HEIGHT, 4);
		DrawString (col * CHAR_WIDTH, VID_HEIGHT - CHAR_WIDTH, string);
	}
//	else
//		re.DrawFill (0, VID_HEIGHT-8, VID_WIDTH, 8, 0);
}

bool Menu_SelectItem (menuFramework_t *s)
{
	menuCommon_t *item;

	item = Menu_ItemAtCursor (s);
	if (item)
	{
		switch (item->type)
		{
		case MTYPE_FIELD:
			return Field_DoEnter( ( menuField_t * ) item ) ;
		case MTYPE_ACTION:
			Action_DoEnter( ( menuAction_t * ) item );
			return true;
//		case MTYPE_LIST:
//		case MTYPE_SPINCONTROL:
//		case MTYPE_SPINCONTROL2:
//			return false;
		}
	}
	return false;
}

void Menu_SetStatusBar (menuFramework_t *m, const char *string)
{
	m->statusbar = string;
}

void Menu_SlideItem (menuFramework_t *s, int dir)
{
	menuCommon_t *item = Menu_ItemAtCursor (s);

	if (item)
	{
		switch (item->type)
		{
		case MTYPE_SLIDER:
			Slider_DoSlide ((menuSlider_t *) item, dir);
			break;
		case MTYPE_SPINCONTROL:
			SpinControl_DoSlide ((menuList_t *) item, dir);
			break;
		case MTYPE_SPINCONTROL2:
			SpinControl2_DoSlide ((menuList2_t *) item, dir);
			break;
		}
	}
}


static void DrawCaption (menuCommon_t *m)
{
	if (m->name)
		Menu_DrawStringR2L (m->x + m->parent->x + LCOLUMN_OFFSET, m->y + m->parent->y, va(S_GREEN"%s", m->name));
}

static void MenuList_Draw (menuList_t *l)
{
	const char **n;

	DrawCaption (&l->generic);
	n = l->itemnames;
  	re.DrawFill (l->generic.x - 112 + l->generic.parent->x, l->generic.parent->y + l->generic.y + l->curvalue*10 + 10,
  		128, CHAR_HEIGHT+2, 16);

	int y = 0;
	while (*n)
	{
		Menu_DrawStringR2L (l->generic.x + l->generic.parent->x + LCOLUMN_OFFSET, l->generic.y + l->generic.parent->y + y + CHAR_HEIGHT+2,
			va(S_GREEN"%s", *n));

		n++;
		y += CHAR_HEIGHT + 2;
	}
}

static void Separator_Draw (menuSeparator_t *s)
{
	int x = s->generic.x + s->generic.parent->x;
	int y = s->generic.y + s->generic.parent->y;
	const char *name = s->generic.name;
	if (!name) return;
	if (s->generic.flags & QMF_CENTER)
		Menu_DrawStringCenter (x, y, va(S_GREEN"%s", name));
	else
		Menu_DrawStringR2L (x, y, va(S_GREEN"%s", name));
}

static void Slider_DoSlide (menuSlider_t *s, int dir)
{
	s->curvalue += dir;

	if (s->curvalue > s->maxvalue)
		s->curvalue = s->maxvalue;
	else if (s->curvalue < s->minvalue)
		s->curvalue = s->minvalue;

	if (s->generic.callback)
		s->generic.callback( s );
}

#define SLIDER_RANGE 10

static void Slider_Draw (menuSlider_t *s)
{
	DrawCaption (&s->generic);

	s->range = (s->curvalue - s->minvalue) / (float)(s->maxvalue - s->minvalue);
	s->range = bound(s->range, 0, 1);

	re.DrawChar (s->generic.x + s->generic.parent->x + RCOLUMN_OFFSET, s->generic.y + s->generic.parent->y, 128);
	for (int i = 0; i < SLIDER_RANGE; i++)
		re.DrawChar (RCOLUMN_OFFSET + s->generic.x + i*8 + s->generic.parent->x + 8, s->generic.y + s->generic.parent->y, 129);
	re.DrawChar (RCOLUMN_OFFSET + s->generic.x + i*8 + s->generic.parent->x + 8, s->generic.y + s->generic.parent->y, 130);
	re.DrawChar (appRound (8 + RCOLUMN_OFFSET + s->generic.parent->x + s->generic.x + (SLIDER_RANGE-1)*8 * s->range),
		s->generic.y + s->generic.parent->y, 131);
}


static void SpinControl_DoSlide (menuList_t *s, int dir)
{
	s->curvalue += dir;

	if (s->curvalue < 0)
		s->curvalue = 0;
	else if (s->itemnames[s->curvalue] == 0)
		s->curvalue--;

	if (s->generic.callback)
		s->generic.callback (s);
}

static void SpinControl_Draw (menuList_t *s)
{
	int		maxIndex;
	const char	*text;
	char	*newline;

	DrawCaption (&s->generic);

	// check for valid index
	maxIndex = 0;
	while (s->itemnames[maxIndex]) maxIndex++;
	maxIndex--;
	if (s->curvalue > maxIndex)
		s->curvalue = maxIndex;
	// draw value
	text = s->itemnames[s->curvalue];
	if (!(newline = strchr (text, '\n')))
		DrawString (RCOLUMN_OFFSET + s->generic.x + s->generic.parent->x, s->generic.y + s->generic.parent->y, text);
	else
	{
		char	buffer[256];

		appStrncpyz (buffer, text, newline - text + 1);	// copy text till '\n' and put zero to its place
		DrawString (RCOLUMN_OFFSET + s->generic.x + s->generic.parent->x, s->generic.y + s->generic.parent->y, buffer);
		DrawString (RCOLUMN_OFFSET + s->generic.x + s->generic.parent->x, s->generic.y + s->generic.parent->y + 10, newline+1);
	}
}


static void SpinControl2_DoSlide (menuList2_t *s, int dir)
{
	s->curvalue += dir;

	if (s->curvalue < 0)
		s->curvalue = 0;
	else
	{
		CStringItem *item;
		int		i;

		for (i = 0, item = s->itemnames; item && i < s->curvalue; item = item->next, i++) ;
		if (!item) i--;
		s->curvalue = i;
	}

	if (s->generic.callback)
		s->generic.callback (s);
}

static void SpinControl2_Draw (menuList2_t *s)
{
	int		i;
	CStringItem *item, *last;
	const char	*text;
	char	*newline;

	DrawCaption (&s->generic);

	// check for valid index
	last = NULL;
	for (i = 0, item = s->itemnames; item && i < s->curvalue; item = item->next, i++) last = item;
	if (!item)	// out of list
	{
		if (!last) return;		// empty list?
		s->curvalue = i - 1;
		item = last;
	}
	// draw value
	text = item->name;
	if (!(newline = strchr (text, '\n')))
		DrawString (RCOLUMN_OFFSET + s->generic.x + s->generic.parent->x, s->generic.y + s->generic.parent->y, text);
	else
	{
		char	buffer[256];

		appStrncpyz (buffer, text, newline - text + 1);	// copy text till '\n' and put zero to its place
		DrawString (RCOLUMN_OFFSET + s->generic.x + s->generic.parent->x, s->generic.y + s->generic.parent->y, buffer);
		DrawString (RCOLUMN_OFFSET + s->generic.x + s->generic.parent->x, s->generic.y + s->generic.parent->y + 10, newline+1);
	}
}
