#include "windows.h"
#include "wprivate.h"

#if(ENABLE_X11 == 1)

int MessageBoxX11( const char* title, const char* text );

int WINAPI MessageBox(
  _In_opt_ HWND    hWnd,
  _In_opt_ LPCTSTR lpText,
  _In_opt_ LPCTSTR lpCaption,
  _In_     UINT    uType
)
{
   (void)hWnd;
   if ((uType & 0x0fu) != MB_OK) {
      SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
      return 0;
   }
   SetLastError(ERROR_SUCCESS);
   return MessageBoxX11(lpCaption ? lpCaption : "", lpText ? lpText : "");
}

/*
   I, David Oberhollenzer, author of this file hereby place the contents of
   this file into the public domain. Please feel free to use this file in any
   way you wish.
   I want to do this, because a lot of people are in the need of a simple X11
   message box function.
   The original version was written in C++ and has been ported to C. This
   version is entirely leak proof! (According to valgrind)
 */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <string.h>
#include <stdlib.h>


typedef struct
{
    int x, y;
    unsigned int width, height;
    int textx, texty;
    int mouseover;
    int clicked;
    const char* text;
}
button;



static void draw_button( button* b, int fg, int bg,
                         Display* dpy, Window w, GC gc )
{
    if( b->mouseover )
    {
        XFillRectangle( dpy, w, gc, b->clicked+b->x, b->clicked+b->y,
                                    b->width, b->height );
        XSetForeground( dpy, gc, bg );
        XSetBackground( dpy, gc, fg );
    }
    else
    {
        XSetForeground( dpy, gc, fg );
        XSetBackground( dpy, gc, bg );
        XDrawRectangle( dpy, w, gc, b->x, b->y, b->width, b->height );
    }

    XDrawString( dpy, w, gc, b->clicked+b->textx, b->clicked+b->texty,
                 b->text, strlen(b->text) );
    XSetForeground( dpy, gc, fg );
    XSetBackground( dpy, gc, bg );
}

static int is_point_inside( button* b, int px, int py )
{
    return px>=b->x && px<=(b->x+(int)b->width-1) &&
           py>=b->y && py<=(b->y+(int)b->height-1);
}



/**************************************************************************
 * A "small" and "simple" function that creates a message box with an OK  *
 * button, using ONLY Xlib.                                               *
 * The function does not return until the user closes the message box,    *
 * using the OK button, the escape key, or the close button what means    *
 * that you can't do anything in the mean time(in the same thread).       *
 * The code may look very ugly, because I pieced it together from         *
 * tutorials and manuals and I use an awfull lot of magic values and      *
 * unexplained calculations.                                              *
 *                                                                        *
 * title: The title of the message box.                                   *
 * text:  The contents of the message box. Use '\n' as a line terminator. *
 **************************************************************************/
int MessageBoxX11( const char* title, const char* text )
{
    const char* wmDeleteWindow = "WM_DELETE_WINDOW";
    const char* wmProtocols = "WM_PROTOCOLS";
    int black, white, height = 0, direction, ascent, descent, X, Y, W=0, H;
    int result = 1; /* IDOK */
    size_t i, lines = 0;
    const char *end, *temp;
    button ok;
    Display* dpy;
    Window w;
    Atom wmDelete;
    GC gc;
    XFontStruct* font;
    XCharStruct overall;
    XSizeHints hints;
    XEvent e;

    /* Open a display. If no X server is available, retain a usable fallback
     * rather than reporting a successful dialog that was never displayed. */
    if( !(dpy = XOpenDisplay( 0 )) ) {
        fprintf(stderr, "[%s] %s\n", title ? title : "", text ? text : "");
        return 1; /* IDOK */
    }
    if (title == NULL) title = "";
    if (text == NULL) text = "";

    /* Get us a white and black color */
    black = BlackPixel( dpy, DefaultScreen(dpy) );
    white = WhitePixel( dpy, DefaultScreen(dpy) );

    /* Create a window with the specified title */
    w = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 100, 100,
                            0, black, black);
    if (w == 0) {
        XCloseDisplay(dpy);
        SetLastError(ERROR_GEN_FAILURE);
        return 0;
    }

    XSelectInput( dpy, w, ExposureMask | StructureNotifyMask |
                          KeyReleaseMask | PointerMotionMask |
                          ButtonPressMask | ButtonReleaseMask );

    XMapWindow( dpy, w );
    XStoreName( dpy, w, title );

    wmDelete = XInternAtom(dpy, wmDeleteWindow, False);
    XSetWMProtocols(dpy, w, &wmDelete, 1);

    /* Create a graphics context for the window */
    gc = XCreateGC(dpy, w, 0, 0);
    if (gc == 0) {
        XDestroyWindow(dpy, w);
        XCloseDisplay(dpy);
        SetLastError(ERROR_GEN_FAILURE);
        return 0;
    }

    XSetForeground( dpy, gc, white );
    XSetBackground( dpy, gc, black );

    /* Compute the printed width and height of the text */
    if (!(font = XQueryFont(dpy, XGContextFromGC(gc)))) {
        result = 0;
        SetLastError(ERROR_GEN_FAILURE);
        goto cleanup;
    }

    for( temp=text; temp; temp = end ? (end+1) : NULL, ++lines )
    {
        end = strchr( temp, '\n' );

        XTextExtents( font, temp, end ? (unsigned int)(end-temp):strlen(temp),
                      &direction, &ascent, &descent, &overall );

        W = overall.width>W ? overall.width : W;
        height = (ascent+descent)>height ? (ascent+descent) : height;
    }

    /* Compute the shape of the window and adjust the window accordingly */
    W += 20;
    H = lines*height + height + 40;
    X = DisplayWidth ( dpy, DefaultScreen(dpy) )/2 - W/2;
    Y = DisplayHeight( dpy, DefaultScreen(dpy) )/2 - H/2;

    XMoveResizeWindow( dpy, w, X, Y, W, H );

    /* Compute the shape of the OK button */
    XTextExtents( font, "OK", 2, &direction, &ascent, &descent, &overall );

    ok.width = overall.width + 30;
    ok.height = ascent + descent + 5;
    ok.x = W/2 - ok.width/2;
    ok.y = H   - height - 15;
    ok.textx = ok.x + 15;
    ok.texty = ok.y + ok.height - 3;
    ok.mouseover = 0;
    ok.clicked = 0;
    ok.text = "OK";

    XFreeFontInfo( NULL, font, 1 ); /* We don't need that anymore */

    /* Make the window non resizeable */
    XUnmapWindow( dpy, w );

    hints.flags      = PSize | PMinSize | PMaxSize;
    hints.min_width  = hints.max_width  = hints.base_width  = W;
    hints.min_height = hints.max_height = hints.base_height = H;

    XSetWMNormalHints( dpy, w, &hints );
    XMapRaised( dpy, w );
    XFlush( dpy );

    /* Event loop */
    while( 1 )
    {
        XNextEvent( dpy, &e );
        ok.clicked = 0;

        if( e.type == MotionNotify )
        {
            if( is_point_inside( &ok, e.xmotion.x, e.xmotion.y ) )
            {
                if( !ok.mouseover )
                    e.type = Expose;

                ok.mouseover = 1;
            }
            else
            {
                if( ok.mouseover )
                    e.type = Expose;

                ok.mouseover = 0;
                ok.clicked = 0;
            }
        }

        switch( e.type )
        {
        case ButtonPress:
        case ButtonRelease:
            if( e.xbutton.button!=Button1 )
                break;

            if( ok.mouseover )
            {
                ok.clicked = e.type==ButtonPress ? 1 : 0;

                if( !ok.clicked )
                    goto cleanup;
            }
            else
            {
                ok.clicked = 0;
            }
#if defined(__GNUC__) && __GNUC__ >= 7
            __attribute__((fallthrough));
#endif
        case Expose:
        case MapNotify:
            XClearWindow( dpy, w );

            /* Draw text lines */
            for( i=0, temp=text; temp; temp=end ? (end+1) : NULL, i+=height )
            {
                end = strchr( temp, '\n' );

                XDrawString( dpy, w, gc, 10, 10+height+i, temp,
                             end ? (unsigned int)(end-temp) : strlen(temp) );
            }

            /* Draw OK button */
            draw_button( &ok, white, black, dpy, w, gc );
            XFlush( dpy );
            break;

        case KeyRelease:
            if( XLookupKeysym( &e.xkey, 0 ) == XK_Escape )
                goto cleanup;
            break;

        case ClientMessage:
            /* WM_DELETE_WINDOW is carried in data.l[0] of a WM_PROTOCOLS
             * client message; comparing the first character of atom names
             * accepted unrelated messages and could dereference a NULL atom. */
            if (e.xclient.format == 32 &&
                e.xclient.message_type == XInternAtom(dpy, wmProtocols, False) &&
                (Atom)e.xclient.data.l[0] == wmDelete)
                goto cleanup;
            break;
        }
    }

cleanup:
    XFreeGC( dpy, gc );
    XDestroyWindow( dpy, w );
    XCloseDisplay(dpy);
    return result;
}

#else

/* Senza X11: stampa su stderr e restituisce IDOK. */
int WINAPI MessageBox(
  _In_opt_ HWND    hWnd,
  _In_opt_ LPCTSTR lpText,
  _In_opt_ LPCTSTR lpCaption,
  _In_     UINT    uType
)
{
   (void)hWnd;
   if ((uType & 0x0fu) != MB_OK) {
      SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
      return 0;
   }
   fprintf(stderr, "[%s] %s\n", lpCaption ? lpCaption : "", lpText ? lpText : "");
   SetLastError(ERROR_SUCCESS);
   return 1; /* IDOK */
}

#endif
