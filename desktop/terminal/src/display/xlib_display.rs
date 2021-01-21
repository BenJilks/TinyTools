extern crate x11;
use crate::display::cursor::CursorPos;
use crate::display::rune::{Rune, StandardColor, Attribute};
use crate::display::UpdateResult;
use x11::{ xlib, xft, keysym };
use std::{ ptr, mem, collections::HashMap, ffi::CString };
use std::os::raw::{ c_ulong, c_uint, c_char };
use std::time::Instant;

struct Screen
{
    bitmap: xlib::Pixmap,
    draw: *mut xft::XftDraw,
}

impl Default for Screen
{
    fn default() -> Self
    {
        Self
        {
            bitmap: 0,
            draw: ptr::null_mut(),
        }
    }
}

impl Screen
{
    pub fn free(&mut self, display: *mut xlib::Display)
    {
        unsafe
        {
            xlib::XFreePixmap(display, self.bitmap);
            xft::XftDrawDestroy(self.draw);
        }
    }
}

pub struct XLibDisplay
{
    // Xlib
    display: *mut xlib::Display,
    visual: *mut xlib::Visual,
    gc: xlib::GC,
    window: c_ulong,
    cmap: c_ulong,
    last_foreground_color: u32,
    main_screen: Screen,
    
    // Window metrics
    fd: i32,
    width: i32,
    height: i32,
    rows: i32,
    columns: i32,
    is_selecting: bool,
    time_since_last_click: Instant,

    // Xft
    font: *mut xft::XftFont,
    colors: HashMap<u32, xft::XftColor>,

    // Terminal
    font_size: i32,
    font_width: i32,
    font_height: i32,
    font_descent: i32,
}

impl XLibDisplay
{
   
    fn new_screen(&mut self) -> Screen
    {
        let bitmap = unsafe { xlib::XCreatePixmap(self.display, self.window, 
            self.width as u32, self.height as u32, 24) };
        
        let draw = unsafe { xft::XftDrawCreate(self.display, bitmap, 
            self.visual, self.cmap) };
        if draw == ptr::null_mut()
        {
            println!("Unable to create Xft draw");
            assert!(false);
        }

        Screen
        {
            bitmap: bitmap,
            draw: draw,
        }
    }

    fn resize_screens(&mut self)
    {
        self.main_screen.free(self.display);
        self.main_screen = self.new_screen();
    }

    fn create_window(&mut self, title: &str)
    {
        unsafe
        {
            // Open display
            self.display = xlib::XOpenDisplay(ptr::null());
            if self.display == ptr::null_mut()
            {
                println!("Could not open x11 display");
                assert!(false);
            }

            // Grab default info
            let screen = xlib::XDefaultScreen(self.display);
            let root_window = xlib::XDefaultRootWindow(self.display);
        
            // Create window
            let black_pixel = xlib::XBlackPixel(self.display, screen);
            let white_pixel = xlib::XWhitePixel(self.display, screen);
            self.window = xlib::XCreateSimpleWindow(
                self.display, root_window, 0, 0, self.width as u32, self.height as u32, 0, 
                white_pixel, black_pixel);
            if self.window == 0
            {
                println!("Unable to open x11 window");
                assert!(false);
            }

            // Select inputs
            xlib::XSelectInput(self.display, self.window, 
                xlib::ExposureMask | xlib::KeyPressMask | 
                xlib::StructureNotifyMask | xlib::ButtonPressMask | 
                xlib::PointerMotionMask | xlib::ButtonReleaseMask);
            
            // Set title and open window
            let title_c_str = CString::new(title).expect("Failed to create C string");
            xlib::XStoreName(self.display, self.window, title_c_str.as_ptr() as *const u8);
            xlib::XMapWindow(self.display, self.window);

            // Create visual
            let mut visual_info: xlib::XVisualInfo = mem::zeroed();
            if xlib::XMatchVisualInfo(self.display, screen, 24, xlib::TrueColor, &mut visual_info) == 0
            {
                println!("Could not create X11 visual");
                assert!(false);
            }
            self.visual = visual_info.visual;
            self.cmap = xlib::XCreateColormap(self.display, 
                root_window, self.visual, xlib::AllocNone);

            let screen = xlib::XDefaultScreen(self.display);
            self.gc = xlib::XDefaultGC(self.display, screen);
            self.fd = xlib::XConnectionNumber(self.display);
        }
    }

    fn load_font(&mut self)
    {
        assert!(self.display != ptr::null_mut());
        unsafe
        {
            let screen = xlib::XDefaultScreen(self.display);
            let font_name = format!("DejaVu Sans Mono:size={}:antialias=true", self.font_size);
            
            // Create font
            self.font = xft::XftFontOpenName(self.display, screen, 
                font_name.as_ptr() as *const c_char);
            if self.font == ptr::null_mut()
            {
                println!("Cannot open font {}", font_name);
                assert!(false);
            }

            self.font_width = (*self.font).max_advance_width;
            self.font_height = (*self.font).height;
            self.font_descent = (*self.font).descent;
        }
    }

    fn free_font(&mut self)
    {
        unsafe
        {
            xft::XftFontClose(self.display, self.font);
        }
    }

    fn reload_font(&mut self)
    {
        self.free_font();
        self.load_font();
    }

    fn font_color(&mut self, color: &u32) -> xft::XftColor
    {
        if !self.colors.contains_key(&color)
        {
            unsafe
            {
                // Create color name string
                let mut font_color: xft::XftColor = mem::zeroed();
                let color_string = format!("#{:06x}", (color >> 8) & 0xFFFFFF);

                // Allocate the color
                let color_c_str = CString::new(color_string).expect("Failed to create C string");
                if xft::XftColorAllocName(self.display, self.visual, self.cmap, 
                    color_c_str.as_ptr() as *const c_char, &mut font_color) == 0
                {
                    println!("Could not allocate Xft color");
                    assert!(false);
                }
                self.colors.insert(*color, font_color);
            }
        }

        return *self.colors.get(&color).unwrap();
    }

    pub fn new(title: &str) -> Self
    {
        let width = 1600;
        let height = 800;

        let mut display = Self
        {
            display: ptr::null_mut(),
            visual: ptr::null_mut(),
            gc: ptr::null_mut(),
            window: 0,
            cmap: 0,
            last_foreground_color: 0,
            main_screen: Screen::default(),

            fd: 0,
            width: width,
            height: height,
            rows: 0,
            columns: 0,
            is_selecting: false,
            time_since_last_click: Instant::now(),

            font: ptr::null_mut(),
            colors: HashMap::new(),

            font_size: 20,
            font_width: 0,
            font_height: 0,
            font_descent: 0,
        };

        display.create_window(title);
        display.load_font();
        display.main_screen = display.new_screen();
        return display;
    }

    fn draw_rect(&mut self, x: i32, y: i32, width: i32, height: i32, color: u32)
    {
        unsafe
        {
            if self.last_foreground_color != color 
            {
                xlib::XSetForeground(self.display, self.gc, ((color & 0xFFFFFF00) >> 8) as u64);
                self.last_foreground_color = color;
            }
            xlib::XFillRectangle(self.display, self.main_screen.bitmap, self.gc, 
                x, y, width as u32, height as u32);
        }
    }

    fn draw_runes_impl(&mut self, runes: &[(Rune, CursorPos)])
    {
        let mut draw_calls = HashMap::<u32, Vec<xft::XftGlyphFontSpec>>::new();
        for (rune, pos) in runes
        {
            let x = pos.get_column() * self.font_width;
            let y = (pos.get_row() + 1) * self.font_height;

            // Draw background
            self.draw_rect(
                x, y - self.font_height, 
                self.font_width, self.font_height,
                rune.attribute.background);

            // If it's a 0, don't draw it
            if rune.code_point == 0 {
                continue;
            }

            // Fetch char data
            let glyph = unsafe { xft::XftCharIndex(self.display, self.font, rune.code_point) };
            let spec = xft::XftGlyphFontSpec 
            { 
                glyph: glyph,
                x: x as i16,
                y: (y - self.font_descent) as i16,
                font: self.font,
            };

            // Add to draw calls
            let color = rune.attribute.foreground;
            if !draw_calls.contains_key(&color) {
                draw_calls.insert(color, Vec::new());
            }
            draw_calls.get_mut(&color).unwrap().push(spec);
        }

        if draw_calls.is_empty() {
            return;
        }

        for (color, specs) in draw_calls.clone()
        {
            let mut xft_color = self.font_color(&color);
            unsafe
            {
                xft::XftDrawGlyphFontSpec(self.main_screen.draw, 
                    &mut xft_color, specs.as_ptr(), specs.len() as i32);
            }
        }
    }

    fn draw_scroll_down(&mut self, amount: i32, top: i32, bottom: i32, height: i32)
    {
        unsafe
        {
            xlib::XCopyArea(self.display, self.main_screen.bitmap, self.main_screen.bitmap, self.gc,
                0, top + amount, 
                self.width as u32, (height - amount) as u32, 
                0, top);
        }

        self.draw_rect(0, bottom - amount, self.width, amount, 
            StandardColor::DefaultBackground.color());
    }

    fn draw_scroll_up(&mut self, amount: i32, top: i32, height: i32)
    {
        unsafe
        {
            xlib::XCopyArea(self.display, self.main_screen.bitmap, self.main_screen.bitmap, self.gc,
                0, top,
                self.width as u32, (height - self.font_height) as u32,
                0, top + amount);
        }
    
        self.draw_rect(0, top, self.width, amount, 
            StandardColor::DefaultBackground.color());
    }

    fn draw_scroll_impl(&mut self, amount: i32, top: i32, bottom: i32)
    {
        let amount_pixels = amount * self.font_height;
        let top_pixels = top * self.font_height;
        let bottom_pixels = bottom * self.font_height;
        let height_pixels = bottom_pixels - top_pixels;
        
        if amount < 0 
        {
            self.draw_scroll_down(-amount_pixels, 
                top_pixels, bottom_pixels, height_pixels);
        }
        else
        {
            self.draw_scroll_up(amount_pixels, 
                top_pixels, height_pixels);
        }
    }

    fn change_font_size(&mut self, font_size: i32) -> UpdateResult
    {
        self.font_size = font_size;
        self.reload_font();
        self.resize_screens();

        self.rows = self.height / self.font_height;
        self.columns = self.width / self.font_width;
        return UpdateResult::resize(self.rows, self.columns, self.width, self.height);
    }

    fn on_key_pressed(&mut self, event: &mut xlib::XEvent) -> UpdateResult
    {
        let state = unsafe { event.key.state };
        let shift = state & xlib::ShiftMask != 0;
        let ctrl_shift = state & xlib::ControlMask != 0 && shift;
        
        let keysym = unsafe { xlib::XkbKeycodeToKeysym(self.display, event.key.keycode as u8, 0, 0) };
        match keysym as c_uint
        {
            keysym::XK_Up => return UpdateResult::input_str("\x1b[A"),
            keysym::XK_Down => return UpdateResult::input_str("\x1b[B"),
            keysym::XK_Right => return UpdateResult::input_str("\x1b[C"),
            keysym::XK_Left => return UpdateResult::input_str("\x1b[D"),

            keysym::XK_Home => return UpdateResult::input_str("\x1b[H"),
            keysym::XK_End => return UpdateResult::input_str("\x1b[F"),
            
            keysym::XK_Page_Up => 
            {
                if shift {
                    return UpdateResult::scroll_viewport(self.rows);
                }
                return UpdateResult::input_str("\x1b[5~");
            },
            keysym::XK_Page_Down => 
            {
                if shift {
                    return UpdateResult::scroll_viewport(-self.rows);
                }
                return UpdateResult::input_str("\x1b[6~");
            },
            keysym::XK_Tab => return UpdateResult::input_str("\t"),
            keysym::XK_Escape => return UpdateResult::input_str("\x1b"),

            keysym::XK_equal =>
            {
                if ctrl_shift {
                    return self.change_font_size(self.font_size + 1);
                }
            },
            keysym::XK_minus =>
            {
                if ctrl_shift {
                    return self.change_font_size(self.font_size - 1);
                }
            },
            _ => {},
        }

        let mut buffer: [u8; 80];
        let len: i32;
        unsafe
        {
            buffer = mem::zeroed();
            len = xlib::XLookupString(&mut event.key,
                buffer.as_mut_ptr() as *mut u8, buffer.len() as i32,
                ptr::null_mut(), ptr::null_mut());
        }
         
        return UpdateResult::input(&buffer[..len as usize]);
    }

    fn on_resize(&mut self, event: &mut xlib::XEvent, results: &mut Vec<UpdateResult>)
    {
        self.width = unsafe { event.configure.width };
        self.height = unsafe { event.configure.height };
        self.rows = self.height / self.font_height;
        self.columns = self.width / self.font_width;
        self.resize_screens();

        results.push(UpdateResult::resize(self.rows, self.columns, self.width, self.height));
    }

    fn on_button_pressed(&mut self, event: &xlib::XEvent, results: &mut Vec<UpdateResult>)
    {
        let button = unsafe { event.button.button };
        match button
        {
            xlib::Button4 => results.push(UpdateResult::scroll_viewport(1)),
            xlib::Button5 => results.push(UpdateResult::scroll_viewport(-1)),
            xlib::Button1 =>
            {
                let (x, y) = unsafe { (event.button.x, event.button.y) };
                let row = y / self.font_height;
                let column = x / self.font_width;

                if self.time_since_last_click.elapsed().as_millis() < 200 {
                    results.push(UpdateResult::double_click_down(row, column));
                } else {
                    results.push(UpdateResult::mouse_down(row, column));
                }
                self.time_since_last_click = Instant::now();
                self.is_selecting = true;
            },

            _ => {},
        }
    }
    fn on_button_released(&mut self, event: &xlib::XEvent)
    {
        let button = unsafe { event.button.button };
        match button
        {
            xlib::Button1 => self.is_selecting = false,
            _ => {},
        }
    }

    fn on_mouse_move(&mut self, event: &xlib::XEvent, results: &mut Vec<UpdateResult>)
    {
        if self.is_selecting
        {
            let (x, y) = unsafe { (event.motion.x, event.motion.y) };
            let row = y / self.font_height;
            let column = x / self.font_width;
            results.push(UpdateResult::mouse_drag(row, column));
        }
    }
    
}

impl Drop for XLibDisplay
{
    
    fn drop(&mut self)
    {
        unsafe
        {
            for color in &mut self.colors
            {
                xft::XftColorFree(self.display, 
                    self.visual, self.cmap, color.1);
            }
            self.free_font();
            self.main_screen.free(self.display);

            xlib::XDestroyWindow(self.display, self.window);
            xlib::XCloseDisplay(self.display);
        }
    }

}

impl super::Display for XLibDisplay
{

    fn update(&mut self) -> Vec<UpdateResult>
    {
        let mut results = Vec::<UpdateResult>::new();
        let mut event: xlib::XEvent = unsafe { mem::zeroed() };
        while unsafe { xlib::XPending(self.display) } != 0
        {
            unsafe { xlib::XNextEvent(self.display, &mut event) };

            match event.get_type()
            {
                xlib::Expose => results.push(UpdateResult::redraw()),
                xlib::KeyPress => results.push(self.on_key_pressed(&mut event)),
                xlib::ConfigureNotify => self.on_resize(&mut event, &mut results),
                xlib::ButtonPress => self.on_button_pressed(&event, &mut results),
                xlib::ButtonRelease => self.on_button_released(&event),
                xlib::MotionNotify => self.on_mouse_move(&event, &mut results),
                _ => {},
            }
        }

        return results;
    }

    fn clear_screen(&mut self)
    {
        self.draw_rect(
            0, 0, self.width, self.height, 
            StandardColor::DefaultBackground.color());
    }

    fn draw_runes(&mut self, runes: &[(Rune, CursorPos)])
    {
        self.draw_runes_impl(runes);
    }

    fn draw_scroll(&mut self, amount: i32, top: i32, bottom: i32)
    {
        self.draw_scroll_impl(amount, top, bottom);
    }
    
    fn draw_clear(&mut self, attribute: &Attribute, 
        row: i32, column: i32, width: i32, height: i32)
    {
        self.draw_rect(
            column * self.font_width, row * self.font_height, 
            width * self.font_width, height * self.font_height, 
            attribute.background);
    }
    
    fn flush(&mut self)
    {
        unsafe
        {
            xlib::XCopyArea(self.display, self.main_screen.bitmap, self.window, self.gc, 
                0, 0, self.width as u32, self.height as u32, 0, 0);
            xlib::XFlush(self.display);
        }
    }

    fn should_close(&self) -> bool
    {
        return false;
    }

    fn get_fd(&self) -> i32
    {
        return self.fd;
    }

}
