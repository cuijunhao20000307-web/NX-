from pathlib import Path

main = Path('nxanime_source/main.cpp')
m = main.read_text(encoding='utf-8')

# Insert touch helper after the v0.3.2 do_search function and before main().
marker = '\nint main(int argc,char** argv){\n'
helper = r'''
static bool hit_box(u32 x,u32 y,int bx,int by,int bw,int bh){
    return x>=(u32)bx&&x<(u32)(bx+bw)&&y>=(u32)by&&y<(u32)(by+bh);
}

static void handle_touch(State& st,Framebuffer& fb,u32 tx,u32 ty){
    if(st.screen==SAFE){
        // Tap the provider/status card to connect.
        if(hit_box(tx,ty,42,220,1196,90)){
            st.status="CONNECTING GIRIGIRI...";
            draw_frame(fb,st);
            connect_source(st,fb);
        }
        return;
    }

    if(st.screen==CATALOG){
        if(!st.view.empty()){
            const int rows=7;
            int start=st.selected>=rows?st.selected-rows+1:0;

            // Anime list: tap to select. Metadata/cover are loaded only for the tapped row.
            if(hit_box(tx,ty,40,190,820,434)){
                int row=(int)(ty-190)/62;
                int vi=start+row;
                if(vi>=0&&vi<(int)st.view.size()){
                    st.selected=vi;
                    resolve_visible(st,fb);
                    ensure_current_cover(st,fb);
                }
                return;
            }

            // Tapping the cover panel opens the selected anime detail.
            if(hit_box(tx,ty,890,190,350,390)){
                open_detail(st,fb);
                return;
            }
        }

        // Footer touch actions: detail / search / refresh.
        if(ty>=640){
            if(tx>=760&&tx<920){
                if(!st.view.empty())open_detail(st,fb);
            }else if(tx>=920&&tx<1080){
                do_search(st,fb);
            }else if(tx>=1080){
                refresh(st,fb);
            }
        }
        return;
    }

    if(st.screen==DETAIL){
        int n=(int)st.detail.episodes.size();
        if(n>0){
            const int cols=6;
            int start=(st.ep/18)*18;
            // Episode grid is 6 x 3. Tap once to select; tap selected cell again to open.
            if(tx>=285&&tx<1221&&ty>=388&&ty<598){
                int col=(int)(tx-285)/156;
                int row=(int)(ty-388)/70;
                int cellx=285+col*156;
                int celly=388+row*70;
                if(col>=0&&col<cols&&row>=0&&row<3&&hit_box(tx,ty,cellx,celly,142,58)){
                    int ei=start+row*cols+col;
                    if(ei<n){
                        bool same=(ei==st.ep);
                        st.ep=ei;
                        if(same)st.screen=EPISODE;
                    }
                    return;
                }
            }
        }
        if(ty>=640&&tx<420)st.screen=CATALOG;
        return;
    }

    if(st.screen==EPISODE){
        if(ty>=620||tx<180)st.screen=DETAIL;
    }
}
'''
if marker not in m:
    raise SystemExit('main marker not found')
m = m.replace(marker, helper + marker, 1)

# Initialize touch and button repeater after pad init.
old_init = r'''    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
'''
new_init = r'''    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    PadRepeater nav_repeat{};padRepeaterInitialize(&nav_repeat,25,7);
    bool touch_was_down=false;
'''
if old_init not in m:
    raise SystemExit('pad init block not found')
m = m.replace(old_init, new_init, 1)

# Expand input collection with analog-stick pseudo buttons, hold-repeat, and touch edge detection.
old_input = r'''        padUpdate(&pad);u64 d=padGetButtonsDown(&pad);if(d&HidNpadButton_Plus)break;
'''
new_input = r'''        padUpdate(&pad);
        u64 d=padGetButtonsDown(&pad);
        u64 held=padGetButtons(&pad);
        const u64 nav_mask=HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right|
                           HidNpadButton_StickLUp|HidNpadButton_StickLDown|HidNpadButton_StickLLeft|HidNpadButton_StickLRight|
                           HidNpadButton_StickRUp|HidNpadButton_StickRDown|HidNpadButton_StickRLeft|HidNpadButton_StickRRight;
        padRepeaterUpdate(&nav_repeat,held&nav_mask);
        d|=padRepeaterGetButtons(&nav_repeat);

        // Map both analog sticks to the same directional navigation used by the D-pad.
        if(d&(HidNpadButton_StickLUp|HidNpadButton_StickRUp))d|=HidNpadButton_Up;
        if(d&(HidNpadButton_StickLDown|HidNpadButton_StickRDown))d|=HidNpadButton_Down;
        if(d&(HidNpadButton_StickLLeft|HidNpadButton_StickRLeft))d|=HidNpadButton_Left;
        if(d&(HidNpadButton_StickLRight|HidNpadButton_StickRRight))d|=HidNpadButton_Right;

        HidTouchScreenState touch{};
        bool touch_down=false;
        u32 tx=0,ty=0;
        if(hidGetTouchScreenStates(&touch,1)>0&&touch.count>0){
            touch_down=true;
            tx=touch.touches[0].x;
            ty=touch.touches[0].y;
        }
        bool touch_pressed=touch_down&&!touch_was_down;
        touch_was_down=touch_down;
        if(touch_pressed)handle_touch(st,fb,tx,ty);

        if(d&HidNpadButton_Plus)break;
'''
if old_input not in m:
    raise SystemExit('input block not found')
m = m.replace(old_input, new_input, 1)

m = m.replace('page+" · v0.3.2"', 'page+" · v0.3.3"')
m = m.replace('"SEARCH BUILD 0.3.2"', '"TOUCH BUILD 0.3.3"')
m = m.replace('NXAnime 0.3.2 startup', 'NXAnime 0.3.3 startup')
main.write_text(m, encoding='utf-8')

print('v0.3.3 joystick + touch patch applied')
