from pathlib import Path

main = Path('nxanime_source/main.cpp')
m = main.read_text(encoding='utf-8')

start = m.find('static void draw_search(u32* b,const State& st){')
end = m.find('\nstatic void draw_filter(u32* b,const State& st){', start)
if start < 0 or end < 0:
    raise SystemExit('draw_search block not found')

new_draw = r'''static void draw_search(u32* b,const State& st){
    header(b,"搜索");
    u32 white=rgba(247,247,250),muted=rgba(148,153,168),pink=rgba(255,62,126);
    u32 card=rgba(24,27,37),card2=rgba(30,33,44),edge=rgba(61,66,82),soft=rgba(42,45,58);

    text(b,58,142,31,"搜索番剧",white,330);
    text(b,58,176,16,"输入部分片名即可匹配 · 例如「东京」「鬼灭」「辉夜」",muted,860);

    // Large search field
    rect(b,g_stride,58,205,1164,112,card);
    border(b,g_stride,58,205,1164,112,2,edge);
    rect(b,g_stride,58,205,8,112,pink);
    text(b,88,244,15,"当前搜索关键词",muted,260);
    text(b,88,289,27,st.search.empty()?"点击 A 输入关键词":st.search,white,900,1);
    rect(b,g_stride,1030,229,150,62,card2);
    border(b,g_stride,1030,229,150,62,2,pink);
    text(b,1061,269,19,"A 搜索",white,100);

    // Primary action row
    rect(b,g_stride,58,352,680,70,card2);
    border(b,g_stride,58,352,680,70,2,pink);
    text(b,88,396,20,"A  输入关键词并搜索",white,560);

    rect(b,g_stride,758,352,210,70,card);
    border(b,g_stride,758,352,210,70,2,edge);
    text(b,806,396,19,"X  返回",white,140);

    rect(b,g_stride,988,352,234,70,card);
    border(b,g_stride,988,352,234,70,2,edge);
    text(b,1020,396,19,"Y  清空",white,170);

    // Search capability card
    rect(b,g_stride,58,452,1164,126,card);
    border(b,g_stride,58,452,1164,126,1,soft);
    text(b,84,488,17,"智能匹配",pink,180);
    text(b,84,522,16,"只输入标题的一部分即可：东京 → 东京食尸鬼    鬼灭 → 鬼灭之刃",white,1040,1);
    text(b,84,553,15,"搜索结果直接读取 GiriGiri AJAX 数据，片名和封面会一起返回。",muted,1040,1);

    footer(b,st,"A 搜索   X 返回   Y 清空");
}
'''

m = m[:start] + new_draw + m[end:]

old_touch = r'''    if(st.screen==SEARCH){
        if(hit_box(tx,ty,70,385,340,58)){execute_search(st,fb);return;}
        if(hit_box(tx,ty,455,385,300,58)){st.screen=CATALOG;return;}
        if(hit_box(tx,ty,800,385,330,58)){return_home(st,fb);return;}
        return;
    }
'''
new_touch = r'''    if(st.screen==SEARCH){
        // Search field / A button / large primary button
        if(hit_box(tx,ty,58,205,1164,112) || hit_box(tx,ty,58,352,680,70)){execute_search(st,fb);return;}
        if(hit_box(tx,ty,758,352,210,70)){st.screen=CATALOG;return;}
        if(hit_box(tx,ty,988,352,234,70)){return_home(st,fb);return;}
        return;
    }
'''
if old_touch not in m:
    raise SystemExit('SEARCH touch block not found')
m = m.replace(old_touch, new_touch, 1)

m = m.replace('page+" · v0.3.7"', 'page+" · v0.3.8"')
m = m.replace('"AJAX SEARCH 0.3.7"', '"SEARCH UI 0.3.8"')
m = m.replace('NXAnime 0.3.7 startup', 'NXAnime 0.3.8 startup')

main.write_text(m, encoding='utf-8')
print('v0.3.8 search UI redesign applied')
