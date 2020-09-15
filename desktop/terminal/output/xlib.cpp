#include "xlib.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <cmath>
#include <sys/time.h>
#include <libprofile/profile.hpp>

#include "../config.hpp"

XLibOutput::XLibOutput()
{
    m_display = XOpenDisplay(nullptr);
    if (!m_display)
    {
        std::cerr << "terminal: xlib: Could not open X11 display\n";
        return;
    }

    m_width = 800;
    m_height = 400;
    m_screen = DefaultScreen(m_display);
    m_depth = 32;

    XVisualInfo visual_info;
    load_back_buffer(visual_info);

    int matches;
    XVisualInfo *match =
        XGetVisualInfo(m_display,
            VisualIDMask|VisualScreenMask|VisualDepthMask,
            &visual_info,
            &matches);

    if (!match || matches < 1) {
        std::cout << "Couldn't match a Visual with double buffering\n";
        return;
    }

    m_visual = match->visual;
    m_color_map = XCreateColormap(m_display, 
        DefaultRootWindow(m_display), m_visual, AllocNone);
    
    XSetWindowAttributes window_attr;
    window_attr.colormap = m_color_map;
    window_attr.background_pixel = TerminalColor::default_color().background_int();
    window_attr.border_pixel = 0;

    auto window_mask = CWBackPixel | CWColormap | CWBorderPixel;
    m_window = XCreateWindow(
        m_display, RootWindow(m_display, m_screen), 
        10, 10, m_width, m_height, 0, 
        CopyFromParent, CopyFromParent, m_visual,
        window_mask, &window_attr);
    m_back_buffer = XdbeAllocateBackBufferName(m_display, m_window, XdbeCopied);

    //m_pixel_buffer = XCreatePixmap(m_display, m_window,
    //    m_width, m_height, m_depth);
    m_gc = XCreateGC(m_display, m_back_buffer, 0, 0);
    
    XSelectInput(m_display, m_window,
        ExposureMask | KeyPressMask | StructureNotifyMask | 
        ButtonPress | ButtonReleaseMask | PointerMotionMask);
     
    auto im = XOpenIM(m_display, nullptr, nullptr, nullptr);
    m_input_context = XCreateIC(im, XNInputStyle, 
        XIMPreeditNothing | XIMStatusNothing,
        XNClientWindow, m_window,
        NULL);
    
    load_font(font_name.c_str(), font_size);
    XMapWindow(m_display, m_window);
    XStoreName(m_display, m_window, "terminal");

    m_wm_delete_message = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(m_display, m_window, &m_wm_delete_message, 1);
    m_clip_board = std::make_unique<XClipBoard>(m_display, m_window);
}

void XLibOutput::init()
{
    did_resize();
}

void XLibOutput::load_back_buffer(XVisualInfo &info)
{
    int num_of_screens = 0;
    auto *screens = &DefaultRootWindow(m_display);

    XdbeScreenVisualInfo *screen_info = XdbeGetVisualInfo(
        m_display, screens, &num_of_screens);
    if (!screen_info || num_of_screens < 1 || screen_info->count < 1) {
        fprintf(stderr, "No visuals support Xdbe\n");
        return;
    }

    info.visualid = screen_info->visinfo[0].visual;
    info.screen = 0;
    info.depth = screen_info->visinfo[0].depth;
}

void XLibOutput::load_font(const std::string &&name, int size)
{
    m_font = XftFontOpenName(m_display, m_screen,  
        (name + ":size=" + std::to_string(size) + ":antialias=true").c_str());
    if (!m_font)
    {
        std::cerr << "terminal: xft: Couln't load font\n";
        return;
    }

    // NOTE: We're assuming monospaced fonts for now
    m_font_width = m_font->max_advance_width;
    m_font_height = m_font->height;
    
    // Helper lambda for loading Xft colors
    auto load_color = [&](XftColor &color, const std::string &hex)
    {
        auto str = "#" + hex;
        if (hex.length() > 6)
            str = "#" + hex.substr(2, hex.length() - 2);

        if (!XftColorAllocName(m_display, m_visual, m_color_map, str.c_str(), &color))
        {
            std::cerr << "terminal: xft: Could not allocate font color: " << str << "\n";
            return;
        }
    };

    // Load the full 8 color pallet
    load_color(m_text_pallet.default_background, ColorPalette::DefaultBackground);
    load_color(m_text_pallet.default_foreground, ColorPalette::DefaultForeground);
    load_color(m_text_pallet.black, ColorPalette::Black);
    load_color(m_text_pallet.red, ColorPalette::Red);
    load_color(m_text_pallet.green, ColorPalette::Green);
    load_color(m_text_pallet.yellow, ColorPalette::Yellow);
    load_color(m_text_pallet.blue, ColorPalette::Blue);
    load_color(m_text_pallet.magenta, ColorPalette::Magenta);
    load_color(m_text_pallet.cyan, ColorPalette::Cyan);
    load_color(m_text_pallet.white, ColorPalette::White);
    
    m_draw = XftDrawCreate(m_display, m_back_buffer, m_visual, m_color_map);
    if (!m_draw)
    {
        std::cerr << "terminal: xft: Could not create draw\n";
        return;
    }
}

CursorPosition XLibOutput::cursor_position_from_pixels(int x, int y)
{
    return CursorPosition(
        roundf(x / m_font_width),
        roundf(y / m_font_height));
}

void XLibOutput::input(const std::string &msg)
{
    m_input_buffer += msg;
}

static uint64_t current_time_in_milliseconds()
{
    struct timeval time;
    gettimeofday(&time, NULL);
    return time.tv_sec * 1000000 + time.tv_usec;
}

void XLibOutput::did_resize()
{
    auto rows = (m_height / m_font_height);
    auto columns = (m_width / m_font_width) - 1;

    // Noop, so don't bother resizing anything
    if (rows == this->rows() && columns == this->columns())
        return;

    if (on_resize)
    {
        // Tell the terminal program that we've resized
        struct winsize size;
        size.ws_row = rows;
        size.ws_col = columns - 1;
        size.ws_xpixel = m_width;
        size.ws_ypixel = m_height;

        resize(rows, columns);
        on_resize(size);
    }

    // Reallocate back buffer
    m_back_buffer = XdbeAllocateBackBufferName(m_display, m_window, XdbeCopied);

    // We need to tell Xft about the new buffer
    XftDrawDestroy(m_draw);
    m_draw = XftDrawCreate(m_display, m_back_buffer,
        m_visual, m_color_map);
}

std::string XLibOutput::update()
{
    if (!m_input_buffer.empty())
    {
        auto msg = m_input_buffer;
        m_input_buffer.clear();

        return msg;
    }

    XEvent event;
    while (XNextEvent(m_display, &event) == 0)
    {
        switch (event.type)
        {
            case ClientMessage:
                if ((Atom)event.xclient.data.l[0] == m_wm_delete_message)
                    set_should_close(true);
                break;

            case Expose:
                redraw_all();
                break;
            
            case KeyPress:
                return decode_key_press(&event.xkey);

            case KeyRelease:
                return decode_key_release(&event.xkey);

            case ButtonPress:
                switch (event.xbutton.button)
                {
                    case Button1:
                        if (current_time_in_milliseconds() - m_time_after_last_click < 200 * 1000)
                        {
                            // Double click
                            select_word_under_mouse();
                            m_time_after_last_click = current_time_in_milliseconds();
                            m_in_selection = true;
                            break;
                        }

                        for_rune_in_selection([this](const CursorPosition &pos)
                        {
                            draw_rune(pos);
                        });
                        m_selection_start = m_mouse_pos;
                        m_selection_end = m_mouse_pos;
                        m_in_selection = true;
                        flush_display();
                        m_time_after_last_click = current_time_in_milliseconds();
                        break;

                    case Button2:
                        return paste();
                        break;

                    case Button3:
                        copy();
                        break;
                    
                    case Button4:
                        if (-m_scroll_offset < buffer().scroll_back() - 1)
                        {
                            m_scroll_offset -= 1;
                            draw_scroll(0, rows(), -1);
                            draw_row(0, true);
                            flush_display();
                        }
                        break;

                    case Button5:
                        if (m_scroll_offset < 0)
                        {
                            m_scroll_offset += 1;
                            draw_scroll(0, rows(), 1);
                            draw_row(rows() - 1, true);
                            flush_display();
                        }
                        break;
                }
                break;

            case ButtonRelease:
                switch (event.xbutton.button)
                {
                    case Button1:
                        m_in_selection = false;
                        break;
                }
                break;
            
            case MotionNotify:
            {
                auto x = event.xmotion.x;
                auto y = event.xmotion.y;
                m_mouse_pos = cursor_position_from_pixels(x, y);

                if (m_in_selection && m_selection_end != m_mouse_pos)
                    draw_update_selection(m_mouse_pos);
                break;
            }
             
            case ConfigureNotify:
                if (m_width != event.xconfigure.width || 
                    m_height != event.xconfigure.height)
                {
                    m_width = event.xconfigure.width;
                    m_height = event.xconfigure.height;
                    did_resize();
                }
                break;
        }
        
        break;
    }
    
    return "";
}

void XLibOutput::select_word_under_mouse()
{
    int start = 0;
    int end = columns() - 1;

    for (int i = m_mouse_pos.coloumn(); i >= 0; i--)
    {
        if (isspace(buffer().rune_at(m_mouse_pos.column_offset(i)).value))
        {
            start = i + 1;
            break;
        }
    }

    for (int i = m_mouse_pos.coloumn(); i < columns(); i++)
    {
        if (isspace(buffer().rune_at(m_mouse_pos.column_offset(i)).value))
        {
            end = i;
            break;
        }
    }

    auto new_selection_end = CursorPosition(end, m_mouse_pos.row());
    m_selection_start = CursorPosition(start, m_mouse_pos.row());
    draw_update_selection(new_selection_end);
}

void XLibOutput::copy()
{
    if (m_selection_start != m_selection_end)
    {
        // Find text under selection
        std::string text;
        for_rune_in_selection([&](const CursorPosition &pos)
        {
            text += (char)buffer().rune_at(pos).value;
        });
    }
}

std::string XLibOutput::paste()
{
    if (m_selection_start != m_selection_end)
    {
        std::string text;
        for_rune_in_selection([&](const CursorPosition &pos)
        {
            text += (char)buffer().rune_at(pos).value;
        });

        return text;
    }

    return "";
}

template<typename CallbackFunc>
void XLibOutput::for_rune_in_selection(CallbackFunc callback)
{
    auto start = m_selection_start;
    auto end = m_selection_end;
    if (start.row() > end.row())
        std::swap(start, end);

    for (int row = start.row(); row <= end.row(); row++)
    {
        auto start_column = (row == start.row() ? start.coloumn() : 0);
        auto end_column = (row == end.row() ? end.coloumn() : columns());
        if (start_column > end_column)
            std::swap(start_column, end_column);

        for (int column = start_column; column < end_column; column++)
            callback(CursorPosition(column, row));
    }
}

void XLibOutput::draw_update_selection(const CursorPosition &new_end_pos)
{
    for_rune_in_selection([this](const CursorPosition &pos)
    {
        draw_rune(pos);
    });

    m_selection_end = new_end_pos;
    for_rune_in_selection([this](const CursorPosition &pos)
    {
        draw_rune(pos, RuneMode::Hilighted);
    });
    flush_display();
}

void XLibOutput::draw_row(int row, bool)
{
    for (int column = 0; column < columns(); column++)
    {
        auto pos = CursorPosition(column, row);
        draw_rune(pos);
    }
}

void XLibOutput::redraw_all()
{
    auto color = TerminalColor(TerminalColor::DefaultForeground, TerminalColor::DefaultBackground);    
    
    XSetForeground(m_display, m_gc, color.background_int());
    XFillRectangle(m_display, m_back_buffer, m_gc,
        0, 0, m_width, m_height);

    for (int row = 0; row < rows(); row++)
        draw_row(row);

    draw_rune(cursor(), RuneMode::Cursor);
    flush_display();
}

std::string XLibOutput::decode_key_release(XKeyEvent *key_event)
{
    char buf[64];
    KeySym ksym;
    Status status;

    XmbLookupString(m_input_context, key_event,
        buf, sizeof(buf), &ksym, &status);

    if (application_keys_mode())
    {
        switch (ksym)
        {
            case XK_Up: return "\033A";
            case XK_Down: return "\033B";
            case XK_Right: return "\033C";
            case XK_Left: return "\033D";
            default: break;
        }
    }

    return "";
}

std::string XLibOutput::decode_key_press(XKeyEvent *key_event)
{
    char buf[64];
    KeySym ksym;
    Status status;
    
    auto len = XmbLookupString(m_input_context, key_event, 
        buf, sizeof(buf), &ksym, &status);
    
    if (application_keys_mode())
    {
        // Special application keys
        switch (ksym)
        {
            case XK_Up: return "\033OA";
            case XK_Down: return "\033OB";
            case XK_Right: return "\033OC";
            case XK_Left: return "\033OD";
            default: break;
        }
    }

    switch (ksym)
    {
        case XK_Up: return "\033[A";
        case XK_Down: return "\033[B";
        case XK_Right: return "\033[C";
        case XK_Left: return "\033[D";

        case XK_Home: return "\033[H";
        case XK_End: return "\033[F";
        case XK_Page_Up: return "\033[5~";
        case XK_Page_Down: return "\033[6~";

        case XK_BackSpace: return "\b";
    }
    
    if (len == 0)
        return "";
        
    return std::string(buf, len);
}

void XLibOutput::draw_scroll(int begin, int end, int by)
{
    auto by_pixels = by * m_font_height;
    auto top_of_buffer = begin * m_font_height + by_pixels;
    auto bottom_of_buffer = (end + 1) * m_font_height;
    auto height_of_buffer = bottom_of_buffer - top_of_buffer;

    auto color = TerminalColor(TerminalColor::DefaultForeground, TerminalColor::DefaultBackground);
    if (by > 0)
    {
        // Down
        XCopyArea(m_display, m_back_buffer, m_back_buffer, m_gc,
            0, top_of_buffer, m_width, height_of_buffer, 0, top_of_buffer - by_pixels);

        XSetForeground(m_display, m_gc, color.background_int());
        XFillRectangle(m_display, m_back_buffer, m_gc,
            0, bottom_of_buffer - by_pixels, m_width, by_pixels);
        for (int i = end - by; i <= end; i++)
            draw_row(i, true);
    }
    else
    {
        // Up
        XCopyArea(m_display, m_back_buffer, m_back_buffer, m_gc,
            0, top_of_buffer,
            m_width, height_of_buffer - m_font_height,
            0, top_of_buffer - by_pixels);

        XSetForeground(m_display, m_gc, color.background_int());
        XFillRectangle(m_display, m_back_buffer, m_gc,
            0, top_of_buffer, m_width, by_pixels + m_font_height);
        for (int i = begin; i < -by; i++)
            draw_row(i, true);
    }

    m_selection_start.move_by(0, -by);
    m_selection_end.move_by(0, -by);
}

XftColor &XLibOutput::text_color_from_terminal(TerminalColor color)
{
    switch (color.foreground())
    {
        case TerminalColor::DefaultBackground: return m_text_pallet.default_background;
        case TerminalColor::DefaultForeground: return m_text_pallet.default_foreground;

        case TerminalColor::Black: return m_text_pallet.black;
        case TerminalColor::Red: return m_text_pallet.red;
        case TerminalColor::Green: return m_text_pallet.green;
        case TerminalColor::Yellow: return m_text_pallet.yellow;
        case TerminalColor::Blue: return m_text_pallet.blue;
        case TerminalColor::Magenta: return m_text_pallet.magenta;
        case TerminalColor::Cyan: return m_text_pallet.cyan;
        case TerminalColor::White: return m_text_pallet.white;
    }
    
    return m_text_pallet.white;
}

void XLibOutput::draw_rune(const CursorPosition &pos, RuneMode mode)
{
    Profile::Timer timer("XLibOutput::draw_rune");

    auto &rune = buffer().rune_at_scroll_offset(pos, m_scroll_offset);
    auto color = rune.attribute.color();
    switch (mode)
    {
        case RuneMode::Hilighted:
            color = color.inverted();
        case RuneMode::Cursor:
            color = current_attribute().color().inverted();
        default:
            break;
    }

    auto c = rune.value;
    auto x = (pos.coloumn() + 1) * m_font_width;
    auto y = (pos.row() + 1) * m_font_height;
    {
        Profile::Timer timer("XLibOutput::draw_rune background");
        XSetForeground(m_display, m_gc, color.background_int());
        XFillRectangle(m_display, m_back_buffer, m_gc,
            x, y - m_font_height, m_font_width, m_font_height);
    }

    if (!isspace(rune.value))
    {
        Profile::Timer timer("XLibOutput::draw_rune glyph");
        auto glyph = XftCharIndex(m_display, m_font, c);

        XRectangle rect = { 0, 0, (uint16_t)(m_font_width * 2), (uint16_t)(m_font_height * 2) };
        XftDrawSetClipRectangles(m_draw, x - m_font_width, y - m_font_height, &rect, 1);

        XftGlyphSpec spec = { glyph, (short)x, (short)(y - m_font->descent) };
        XftDrawGlyphSpec(m_draw, &text_color_from_terminal(color),
            m_font, &spec, 1);

        // Reset clip
        XftDrawSetClip(m_draw, 0);
    }
}

void XLibOutput::flush_display()
{
    Profile::Timer timer("XLibOutput::flush_display");

    XdbeSwapInfo swap_info;
    swap_info.swap_window = m_window;
    swap_info.swap_action = XdbeCopied;

    // XdbeSwapBuffers returns true on success
    if (!XdbeSwapBuffers(m_display, &swap_info, 1))
        std::cout << "terminal: xlib: could not swap buffers\n";
    XFlush(m_display);
}

void XLibOutput::on_out_rune(uint32_t)
{
    if (m_scroll_offset == 0)
        return;

    m_scroll_offset = 0;
    redraw_all();
}

void XLibOutput::out_os_command(Decoder::OSCommand &os_command)
{
    switch (os_command.command)
    {
        case 0:
            XStoreName(m_display, m_window, os_command.body.c_str());
            break;

        default:
            std::cout << "terminal: xlib: Unkown os command " << os_command.command << "\n";
            break;
    }
}

int XLibOutput::input_file() const
{
    return XConnectionNumber(m_display);
}

XLibOutput::~XLibOutput()
{
    if (m_font)
        XftFontClose(m_display, m_font);

    if (m_draw)
        XftDrawDestroy(m_draw);
}
