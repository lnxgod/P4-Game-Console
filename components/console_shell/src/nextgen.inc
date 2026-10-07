/* SPDX-License-Identifier: MIT
 * Tab5's native interface. Included by console_shell.c so the presentation
 * reuses the same validated service actions as the compact board interfaces.
 * One scene defines both drawing and hit regions; no bitmap contains controls.
 */
#include "nextgen_assets.h"
#include <stdlib.h>
enum { NG_W=1280, NG_H=720, NG_MAX_CONTROLS=64,
    NC_HOME=1,NC_LIBRARY,NC_MULTIPLAYER,NC_FILES,NC_SETTINGS,NC_BACK,
    NC_PAGE,NC_LAUNCH,NC_PREV,NC_NEXT,NC_FILE_ROW,NC_FILE_PREV,NC_FILE_NEXT,
    NC_REFRESH,NC_FILE_OPEN,NC_DELETE,NC_CANCEL,NC_CONFIRM,NC_AUDIO,
    NC_STORAGE,NC_CONTROLLER,NC_USB,NC_MP_VIEW,NC_MP_OPTION,NC_MP_PRIMARY,
    NC_MP_ROOM,NC_KEY,NC_APPEARANCE,NC_LIST_PREV,NC_LIST_NEXT,NC_MP_OPTIONS,NC_KEYBOARD,NC_FILTER,NC_CATEGORY,NC_CATEGORY_PREV,NC_CATEGORY_NEXT,
    NC_FILE_MENU,NC_DISMISS,NC_SCROLL,NC_AUDIO_STATUS,NC_MP_GAME,NC_MP_TRANSPORT };
enum { NG_GRID_TOP=178, NG_GRID_BOTTOM=626, NG_GRID_PITCH=210,
    NG_FILES_TOP=178, NG_FILES_BOTTOM=536, NG_FILE_PITCH=90 };
typedef struct { uint16_t id; int16_t x,y,w,h; bool enabled,animated; } ng_control_t;
typedef struct {
    console_shell_t *shell; uint16_t *pixels; size_t stride;
    ng_control_t controls[NG_MAX_CONTROLS]; size_t count;
    int clip_top,clip_bottom;
    int origin_x,origin_y,width,height;
    int damage_left,damage_top,damage_right,damage_bottom;
    int raster_scroll;
    bool cache_base,skip_rows;
} ng_scene_t;
#define NG_RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define NG_INK NG_RGB(7,18,28)
#define NG_SURFACE NG_RGB(18,39,53)
#define NG_WHITE NG_RGB(244,250,252)
#define NG_MUTED NG_RGB(180,203,213)
#define NG_CYAN NG_RGB(85,230,236)
#define NG_LINE NG_RGB(47,83,101)
#define NG_AMBER NG_RGB(255,205,95)
static uint16_t ng_id(unsigned kind,unsigned param)
{ return (uint16_t)(kind*128U+param); }
static uint16_t ng_mix(uint16_t a,uint16_t b,unsigned alpha)
{
    const unsigned inv=15U-alpha;
    return (uint16_t)((((((unsigned)a>>11U)*alpha+((unsigned)b>>11U)*inv+7U)/15U)<<11U)|
        ((((((unsigned)a>>5U)&63U)*alpha+(((unsigned)b>>5U)&63U)*inv+7U)/15U)<<5U)|
        ((((unsigned)a&31U)*alpha+((unsigned)b&31U)*inv+7U)/15U));
}
static void ng_clip(const ng_scene_t *s,int *x,int *y,int *right,int *bottom)
{
    const int left=s->origin_x,top=s->origin_y;
    const int limit_x=left+(s->width?s->width:NG_W),limit_y=top+(s->height?s->height:NG_H);
    if(*x<left){*x=left;}
    if(*y<top){*y=top;}
    if(*right>limit_x){*right=limit_x;}
    if(*bottom>limit_y){*bottom=limit_y;}
    if(s->clip_bottom){
        if(*y<s->clip_top){*y=s->clip_top;}
        if(*bottom>s->clip_bottom){*bottom=s->clip_bottom;}
    }
    if(s->damage_right>s->damage_left){
        if(*x<s->damage_left){*x=s->damage_left;}
        if(*right>s->damage_right){*right=s->damage_right;}
        if(*y<s->damage_top){*y=s->damage_top;}
        if(*bottom>s->damage_bottom){*bottom=s->damage_bottom;}
    }
}
static uint16_t *ng_pixel(ng_scene_t *s,int x,int y)
{
    int right=x+1,bottom=y+1,left=x,top=y;ng_clip(s,&left,&top,&right,&bottom);
    if(left>=right||top>=bottom)return NULL;
    return s->pixels+(size_t)(y-s->origin_y)*s->stride+(size_t)(x-s->origin_x);
}
static void ng_rect(ng_scene_t *s,int x,int y,int w,int h,uint16_t color)
{
    if (!s->pixels || w<=0 || h<=0) return;
    int x1=x+w,y1=y+h;ng_clip(s,&x,&y,&x1,&y1);
    if(x>=x1||y>=y1)return;
    for(int row=y;row<y1;++row){
        uint16_t *p=s->pixels+(size_t)(row-s->origin_y)*s->stride+(size_t)(x-s->origin_x);
        for(int col=x;col<x1;++col)*p++=color;
    }
}
static void ng_round(ng_scene_t *s,int x,int y,int w,int h,int radius,uint16_t color)
{
    if(!s->pixels)return;
    for(int row=0;row<h;++row){
        int dy=row<radius?radius-row-1:(row>=h-radius?row-(h-radius):0);
        int cut=0;while(cut<radius && (radius-cut)*(radius-cut)+dy*dy>radius*radius)++cut;
        ng_rect(s,x+cut,y+row,w-cut*2,1,color);
    }
}
static void ng_panel(ng_scene_t *s,int x,int y,int w,int h,uint16_t border,uint16_t fill)
{ ng_round(s,x,y,w,h,14,border);ng_round(s,x+2,y+2,w-4,h-4,12,fill); }
static int ng_round_cut(int row,int height,int radius)
{
    const int dy=row<radius?radius-row-1:(row>=height-radius?row-(height-radius):0);
    int cut=0;while(cut<radius&&(radius-cut)*(radius-cut)+dy*dy>radius*radius)++cut;
    return cut;
}
/* Repaint only the outer panel ring; cached text and artwork stay immutable. */
static void ng_panel_border(ng_scene_t *s,int x,int y,int w,int h,uint16_t color,bool artwork)
{
    for(int row=0;row<h;++row){
        const int cut=ng_round_cut(row,h,14),left=x+cut,right=x+w-cut;
        if(row<2||row>=h-2)ng_rect(s,left,y+row,right-left,1,color);
        else{
            const int inside=ng_round_cut(row-2,h-4,12);
            int inside_left=x+2+inside,inside_right=x+w-2-inside;
            /* Artwork is rectangular and overwrites rounded panel corners in
             * the authoritative draw order; focus must not paint over it. */
            if(artwork&&row>=4&&row<h-61){
                if(inside_left>x+4)inside_left=x+4;
                if(inside_right<x+w-4)inside_right=x+w-4;
            }
            ng_rect(s,left,y+row,inside_left-left,1,color);
            ng_rect(s,inside_right,y+row,right-inside_right,1,color);
        }
    }
}
static void ng_line(ng_scene_t *s,int x,int y,int x1,int y1,uint16_t color)
{
    if(!s->pixels)return;
    int dx=x1>x?x1-x:x-x1,dy=y1>y?y-y1:y1-y;
    int sx=x<x1?1:-1,sy=y<y1?1:-1,err=dx+dy;
    for(;;){ng_rect(s,x,y,3,3,color);if(x==x1&&y==y1)break;
        const int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}
}
typedef struct {int size,cell;const uint8_t *alpha,*advance;} ng_font_t;
static const ng_font_t *ng_font(int size)
{
    static const ng_font_t fonts[]={
        {32,40,ng_font32_alpha,ng_font32_advance},
        {44,52,ng_font44_alpha,ng_font44_advance},
        {68,80,ng_font68_alpha,ng_font68_advance}};
    return &fonts[size>=52?2:size>=36?1:0];
}
static int ng_advance(unsigned c,int size)
{const ng_font_t *font=ng_font(size);return (int)font->advance[c-32U]*size/font->size;}
static int ng_text_width(const char *str,int size)
{
    int width=0;if(!str)return 0;
    for(size_t i=0;i<128U&&str[i];++i){unsigned c=(unsigned char)str[i];if(c<32U||c>126U)c='?';
        width+=ng_advance(c,size);}
    return width;
}
static void ng_text(ng_scene_t *s,int x,int y,const char *str,int size,uint16_t color,int max_width)
{
    if(!s->pixels||!str||size<8||size>96)return;
    const ng_font_t *font=ng_font(size);
    const int side=font->cell*size/font->size;const int limit=x+max_width;
    uint8_t sample[128];
    for(int pos=0;pos<side;++pos)sample[pos]=(uint8_t)(pos*font->cell/side);
    for(size_t i=0;i<128U&&str[i];++i){
        unsigned c=(unsigned char)str[i];if(c<32U||c>126U)c='?';
        int advance=ng_advance(c,size);
        if(x+advance>limit)break;
        int left=x,top=y,right=x+side,bottom=y+side;if(right>limit)right=limit;
        ng_clip(s,&left,&top,&right,&bottom);
        if(left>=right||top>=bottom){x+=advance;continue;}
        for(int row=top;row<bottom;++row){
            const unsigned base=(c-32U)*(unsigned)(font->cell*font->cell)+(unsigned)sample[row-y]*(unsigned)font->cell;
            uint16_t *dst=s->pixels+(size_t)(row-s->origin_y)*s->stride+(size_t)(left-s->origin_x);
            for(int col=left;col<right;++col,++dst){
                const unsigned pos=base+(unsigned)sample[col-x];
                const unsigned packed=font->alpha[pos/2U];
                const unsigned a=pos%2U?packed&15U:packed>>4U;
                if(a)*dst=a==15U?color:ng_mix(color,*dst,a);
            }
        }
        x+=advance;
    }
}
static void ng_label(ng_scene_t *s,int x,int y,const char *text,int size,uint16_t color,int width)
{
    if(ng_text_width(text,size)<=width){ng_text(s,x,y,text,size,color,width);return;}
    char value[128];size_t n=0;int used=0;const int dots=ng_text_width("...",size);
    while(text[n]&&n<sizeof(value)-4U){unsigned c=(unsigned char)text[n];if(c<32U||c>126U)c='?';
        int step=ng_advance(c,size);if(used+step+dots>width)break;
        value[n]=(char)c;used+=step;++n;}
    memcpy(value+n,"...",4U);ng_text(s,x,y,value,size,color,width);
}
static void ng_center(ng_scene_t *s,int x,int y,int w,const char *label,int size,uint16_t color)
{
    int tw=ng_text_width(label,size);if(tw>w-24)tw=w-24;
    ng_label(s,x+(w-tw)/2,y,label,size,color,w-24);
}
static void ng_art(ng_scene_t *s,int x,int y,int w,int h,const void *art,int aw,int ah,const uint16_t *palette)
{
    if(!s->pixels||!art||w<=0||h<=0)return;
    int sw=aw,sh=ah;
    if(aw*h>ah*w)sw=ah*w/h;else sh=aw*h/w;
    const int ox=(aw-sw)/2,oy=(ah-sh)/2;
    int left=x,top=y,right=x+w,bottom=y+h;ng_clip(s,&left,&top,&right,&bottom);
    if(left>=right||top>=bottom)return;
    const int step=sw/w,remainder=sw%w,start=(left-x)*sw;
    for(int row=top;row<bottom;++row){
        const size_t offset=(size_t)(oy+(row-y)*sh/h)*(size_t)aw;
        int sample_x=ox+start/w,fraction=start%w;
        uint16_t *dst=s->pixels+(size_t)(row-s->origin_y)*s->stride+(size_t)(left-s->origin_x);
        for(int col=left;col<right;++col){
            const size_t index=offset+(size_t)sample_x;
            *dst++=palette?palette[((const uint8_t *)art)[index]]:((const uint16_t *)art)[index];
            sample_x+=step;fraction+=remainder;if(fraction>=w){fraction-=w;++sample_x;}
        }
    }
}
static void ng_icon(ng_scene_t *s,int x,int y,unsigned kind,uint16_t color)
{
    if(kind==0U){ng_line(s,x,y+16,x+18,y,color);ng_line(s,x+18,y,x+36,y+16,color);
        ng_rect(s,x+6,y+15,26,24,color);ng_rect(s,x+16,y+25,8,14,NG_INK);}
    else if(kind==1U){ng_round(s,x,y+8,39,29,9,color);ng_round(s,x+3,y+11,33,23,7,NG_INK);
        ng_rect(s,x+8,y+18,13,3,color);ng_rect(s,x+13,y+13,3,13,color);
        ng_rect(s,x+28,y+17,4,4,color);ng_rect(s,x+25,y+24,4,4,color);}
    else if(kind==2U){ng_round(s,x+5,y,14,14,7,color);ng_round(s,x+24,y+5,12,12,6,color);
        ng_round(s,x,y+19,24,20,6,color);ng_round(s,x+25,y+23,14,16,5,color);}
    else if(kind==3U){ng_round(s,x,y+5,18,12,3,color);ng_panel(s,x,y+12,40,28,color,NG_INK);}
    else if(kind==5U){ng_rect(s,x,y+12,12,16,color);ng_line(s,x+10,y+12,x+24,y+2,color);ng_line(s,x+24,y+2,x+24,y+38,color);ng_line(s,x+24,y+38,x+10,y+27,color);ng_line(s,x+32,y+10,x+38,y+18,color);ng_line(s,x+38,y+18,x+32,y+28,color);}
    else if(kind==6U){ng_round(s,x,y,40,40,18,color);ng_round(s,x+12,y+22,22,22,10,NG_SURFACE);ng_round(s,x+8,y+8,7,7,3,NG_INK);ng_round(s,x+24,y+8,7,7,3,NG_INK);}
    else if(kind==7U){ng_panel(s,x+2,y+5,35,31,color,NG_SURFACE);ng_rect(s,x+12,y,14,5,color);ng_rect(s,x+8,y+12,9,18,color);}
    else if(kind==8U){ng_panel(s,x,y+2,40,36,color,NG_SURFACE);ng_line(s,x+4,y+14,x+34,y+14,color);ng_line(s,x+4,y+25,x+34,y+25,color);}
    else {ng_round(s,x+6,y+6,28,28,14,color);ng_round(s,x+14,y+14,12,12,6,NG_INK);
        ng_rect(s,x+17,y,6,9,color);ng_rect(s,x+17,y+31,6,9,color);
        ng_rect(s,x,y+17,9,6,color);ng_rect(s,x+31,y+17,9,6,color);}
}
static void ng_add(ng_scene_t *s,int x,int y,int w,int h,unsigned kind,unsigned param,bool enabled)
{
    if(s->cache_base)return;
    if(s->clip_bottom){int bottom=y+h;if(y<s->clip_top)y=s->clip_top;if(bottom>s->clip_bottom)bottom=s->clip_bottom;h=bottom-y;}
    if(s->count>=NG_MAX_CONTROLS||w<=0||h<=0)return;
    s->controls[s->count++]=(ng_control_t){ng_id(kind,param),(int16_t)x,(int16_t)y,(int16_t)w,(int16_t)h,enabled,false};
}
static bool ng_focused(const ng_scene_t *s,unsigned kind,unsigned param)
{ return !s->cache_base&&s->shell->ng_focus==ng_id(kind,param); }
static void ng_button(ng_scene_t *s,int x,int y,int w,int h,const char *label,
    unsigned kind,unsigned param,bool enabled,bool primary)
{
    const size_t before=s->count;
    ng_add(s,x,y,w,h,kind,param,enabled);
    if(s->count>before)s->controls[s->count-1U].animated=true;
    const bool focused=ng_focused(s,kind,param);
    const bool pressed=s->shell->press_active&&s->shell->pressed_index==ng_id(kind,param);
    const uint16_t fill=enabled&&primary?NG_CYAN:NG_SURFACE;
    ng_panel(s,x,y,w,h,focused&&enabled?ng_mix(NG_WHITE,NG_CYAN,(unsigned)s->shell->ng_focus_ms*15U/100U):(primary&&enabled?NG_CYAN:NG_LINE),fill);
    if(pressed)ng_round(s,x+5,y+5,w-10,h-10,8,ng_mix(NG_WHITE,fill,3));
    int font=s->shell->ng_large_text?32:28;
    /* Preserve recognizable game names in the compact inventory grid. */
    if(kind==NC_MP_GAME)while(font>24&&ng_text_width(label,font)>w-24)font-=2;
    ng_center(s,x,y+(h-font*40/32)/2,w,label,font,enabled?(primary?NG_INK:NG_WHITE):NG_MUTED);
}
static bool ng_contains(const char *text,const char *needle)
{
    if(!text)return false;
    char folded[CONSOLE_SHELL_TITLE_MAX_BYTES+1];size_t n=0;
    for(;n<sizeof(folded)-1U&&text[n];++n){char c=text[n];folded[n]=(c>='a'&&c<='z')?(char)(c-'a'+'A'):c;}
    folded[n]='\0';return strstr(folded,needle)!=NULL;
}
static bool ng_system_utility(const console_app_descriptor_t *app)
{
    return app->page==CONSOLE_PAGE_EXTERNAL&&app->folder_path&&
        (strcmp(app->folder_path,"SYSTEM")==0||strncmp(app->folder_path,"SYSTEM/",7U)==0);
}
static bool ng_game(const console_app_descriptor_t *app)
{ return app->page==CONSOLE_PAGE_EXTERNAL&&!ng_system_utility(app); }
static size_t ng_games(const console_shell_t *sh,size_t *indices)
{
    size_t count=0;const char *preferred[]={"BLAST","WACKY","MAZE","SOLITAIRE"};
    for(size_t p=0;p<4U;++p)for(size_t i=0;i<sh->app_count;++i)
        if(ng_game(&sh->apps[i])&&sh->apps[i].enabled&&ng_contains(sh->apps[i].title,preferred[p])){
            bool seen=false;for(size_t j=0;j<count;++j)if(indices[j]==i)seen=true;
            if(!seen)indices[count++]=i;}
    for(size_t i=0;i<sh->app_count;++i)if(ng_game(&sh->apps[i])){
        bool seen=false;for(size_t j=0;j<count;++j)if(indices[j]==i)seen=true;
        if(!seen)indices[count++]=i;}
    return count;
}
static const char *ng_app_category(const console_app_descriptor_t *app)
{ return app->folder_path&&app->folder_path[0]?app->folder_path:"OTHER"; }
static void ng_category_label(const char *path,char out[CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES])
{
    const char *leaf=strrchr(path,'/');leaf=leaf?leaf+1:path;
    if(strcmp(leaf,"WIP")==0){(void)snprintf(out,CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES,"Work in progress");return;}
    size_t i=0;bool initial=true;
    for(;leaf[i]&&i<CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES-1U;++i){
        char c=leaf[i];out[i]=!initial&&c>='A'&&c<='Z'?(char)(c-'A'+'a'):c;
        initial=c==' '||c=='-';}
    out[i]='\0';
}
static size_t ng_categories(const console_shell_t *sh,const char **paths)
{
    size_t n=1;paths[0]="";
    for(size_t i=0;i<sh->app_count;++i)if(ng_game(&sh->apps[i])){
        const char *path=ng_app_category(&sh->apps[i]);bool seen=false;
        for(size_t j=1;j<n;++j)if(strcmp(paths[j],path)==0)seen=true;
        if(!seen)paths[n++]=path;}
    for(size_t i=2;i<n;++i){const char *path=paths[i];size_t j=i;
        while(j>1U&&strcmp(paths[j-1U],path)>0){paths[j]=paths[j-1U];--j;}paths[j]=path;}
    return n;
}
static size_t ng_library_games(console_shell_t *sh,size_t *indices)
{
    const char *paths[CONSOLE_SHELL_MAX_APPS+1];const size_t categories=ng_categories(sh,paths);
    bool valid=false;for(size_t i=0;i<categories;++i)if(strcmp(paths[i],sh->ng_category)==0)valid=true;
    if(!valid){sh->ng_category[0]='\0';sh->ng_library_scroll=0;}
    const size_t total=ng_games(sh,indices);size_t n=0;
    for(size_t i=0;i<total;++i)if(!sh->ng_category[0]||strcmp(ng_app_category(&sh->apps[indices[i]]),sh->ng_category)==0)indices[n++]=indices[i];
    return n;
}
static int ng_scroll_max(size_t count,bool library)
{
    const int content=library?(int)((count+2U)/3U)*NG_GRID_PITCH-16:(int)count*NG_FILE_PITCH-8;
    const int viewport=library?NG_GRID_BOTTOM-NG_GRID_TOP:NG_FILES_BOTTOM-NG_FILES_TOP;
    return content>viewport?content-viewport:0;
}
static void ng_set_scroll(console_shell_t *sh,bool library,int value)
{
    size_t indices[CONSOLE_SHELL_MAX_APPS];
    const size_t count=library?ng_library_games(sh,indices):sh->files.entry_count;
    const int limit=ng_scroll_max(count,library);if(value<0)value=0;if(value>limit)value=limit;
    int *offset=library?&sh->ng_library_scroll:&sh->ng_file_scroll;
    if(*offset!=value){*offset=value;sh->dirty=true;}
}
static void ng_reveal(console_shell_t *sh,size_t index,bool library)
{
    const int top=library?(int)(index/3U)*NG_GRID_PITCH:(int)index*NG_FILE_PITCH;
    const int height=library?194:82,view=library?NG_GRID_BOTTOM-NG_GRID_TOP:NG_FILES_BOTTOM-NG_FILES_TOP;
    const int offset=library?sh->ng_library_scroll:sh->ng_file_scroll;
    if(top<offset)ng_set_scroll(sh,library,top);
    else if(top+height>offset+view)ng_set_scroll(sh,library,top+height-view);
}
static void ng_scrollbar(ng_scene_t *s,size_t count,bool library)
{
    const int top=library?NG_GRID_TOP:NG_FILES_TOP,bottom=library?NG_GRID_BOTTOM:NG_FILES_BOTTOM;
    const int view=bottom-top,limit=ng_scroll_max(count,library);if(!limit)return;
    const int offset=library?s->shell->ng_library_scroll:s->shell->ng_file_scroll;
    int thumb=view*view/(view+limit);if(thumb<48)thumb=48;
    ng_round(s,1238,top,12,view,6,NG_SURFACE);
    ng_round(s,1238,top+offset*(view-thumb)/limit,12,thumb,6,NG_CYAN);
    ng_add(s,1230,top,30,view,NC_SCROLL,library?1U:2U,true);
}
static void ng_game_art(ng_scene_t *s,const console_app_descriptor_t *app,int x,int y,int w,int h)
{
    if(!s->pixels)return;
    const console_shell_artwork_t *cover=app->cover;
    const bool byte_legacy=ng_contains(app->title,"BYTE")&&
        (!app->icon_pixels||!app->icon_palette||
         (memcmp(app->icon_pixels,console_byte_legacy_icon_pixels,128U*72U)==0&&
          memcmp(app->icon_palette,console_byte_legacy_icon_palette,256U*sizeof(uint16_t))==0));
    if(cover&&cover->pixels&&cover->palette&&cover->width&&cover->height&&
       cover->width<=1024U&&cover->height<=576U)
        ng_art(s,x,y,w,h,cover->pixels,cover->width,cover->height,cover->palette);
    else if(w>400&&byte_legacy)ng_art(s,x,y,w,h,ng_art_byte_buddy,1024,576,ng_art_byte_buddy_palette);
    else if(app->icon_pixels && app->icon_palette)ng_art(s,x,y,w,h,app->icon_pixels,128,72,app->icon_palette);
    else if(ng_contains(app->title,"BYTE"))ng_art(s,x,y,w,h,ng_art_byte_buddy,1024,576,ng_art_byte_buddy_palette);
    else if(ng_contains(app->title,"MAZE"))ng_art(s,x,y,w,h,ng_art_maze,320,180,NULL);
    else if(ng_contains(app->title,"BLAST"))ng_art(s,x,y,w,h,ng_art_blast,320,180,NULL);
    else if(ng_contains(app->title,"SOLITAIRE"))ng_art(s,x,y,w,h,ng_art_solitaire,320,180,NULL);
    else {
        ng_rect(s,x,y,w,h,NG_SURFACE);
        for(int line=0;line<5;++line)ng_line(s,x,y+h-20-line*18,x+w-1,y+h-20-line*18,NG_LINE);
        for(int line=0;line<5;++line)ng_line(s,x+w/2,y+12,x+line*w/4,y+h-1,NG_LINE);
        ng_icon(s,x+w/2-20,y+h/2-22,1U,app->accent_rgb565?app->accent_rgb565:NG_CYAN);
    }
}
static bool ng_game_art_is_generic(const console_app_descriptor_t *app)
{
    const console_shell_artwork_t *cover=app->cover;
    return !(cover&&cover->pixels&&cover->palette&&cover->width&&cover->height&&
        cover->width<=1024U&&cover->height<=576U)&&!(app->icon_pixels&&app->icon_palette)&&
        !ng_contains(app->title,"BYTE")&&!ng_contains(app->title,"MAZE")&&
        !ng_contains(app->title,"BLAST")&&!ng_contains(app->title,"SOLITAIRE");
}
static void ng_game_card(ng_scene_t *s,size_t app_index,int x,int y,int w,int h)
{
    const console_app_descriptor_t *app=&s->shell->apps[app_index];
    ng_add(s,x,y,w,h,NC_LAUNCH,(unsigned)app_index,app->enabled);
    const bool focused=ng_focused(s,NC_LAUNCH,(unsigned)app_index);
    ng_panel(s,x,y,w,h,focused?NG_CYAN:NG_LINE,NG_SURFACE);
    ng_game_art(s,app,x+4,y+4,w-8,h-65);
    ng_label(s,x+16,y+h-53,app->title,s->shell->ng_large_text?30:26,
        app->enabled?NG_WHITE:NG_MUTED,w-32);
    if(!app->enabled)ng_label(s,x+16,y+12,"Unavailable",24,NG_WHITE,w-32);
}
static void ng_library_rows(ng_scene_t *s,const size_t *games,size_t count)
{
    if(s->skip_rows)return;
    const int offset=s->cache_base?s->raster_scroll:s->shell->ng_library_scroll;
    for(size_t i=0;i<count;++i){const int y=NG_GRID_TOP+(int)(i/3U)*NG_GRID_PITCH-offset;
        if(y+194>s->clip_top&&y<s->clip_bottom)ng_game_card(s,games[i],264+(int)(i%3U)*326,y,310,194);}
}
static void ng_file_rows(ng_scene_t *s,size_t count)
{
    if(s->skip_rows)return;
    console_shell_t *sh=s->shell;char value[80];
    for(size_t i=0;i<count;++i){
        const console_shell_file_entry_t *entry=&sh->files.entries[i];int y=NG_FILES_TOP+(int)i*NG_FILE_PITCH-(s->cache_base?s->raster_scroll:sh->ng_file_scroll);
        if(y+82<=s->clip_top||y>=s->clip_bottom)continue;
        ng_add(s,264,y,964,82,NC_FILE_ROW,(unsigned)i,true);
        const bool selected=!s->cache_base&&sh->file_selected_index==i;
        ng_panel(s,264,y,964,82,selected||ng_focused(s,NC_FILE_ROW,(unsigned)i)?NG_CYAN:NG_LINE,NG_SURFACE);
        ng_icon(s,282,y+21,entry->is_directory?3U:1U,NG_CYAN);
        ng_label(s,344,y+18,entry->label,sh->ng_large_text?34:30,NG_WHITE,616);
        (void)snprintf(value,sizeof(value),entry->is_directory?"Folder":"%lu KB",(unsigned long)entry->size_kib);
        ng_label(s,990,y+23,value,25,NG_MUTED,235);
    }
}
static void ng_status_row(ng_scene_t *s,int y,const char *label,const char *value,uint16_t color)
{
    ng_rect(s,280,y+61,948,1,NG_LINE);
    ng_label(s,288,y,label,s->shell->ng_large_text?30:28,NG_MUTED,430);
    ng_label(s,740,y,value,s->shell->ng_large_text?30:28,color,480);
}
static void ng_heading(ng_scene_t *s,const char *title)
{ ng_label(s,264,86,title,44,NG_WHITE,876); }
static void ng_empty(ng_scene_t *s,const char *title,const char *detail)
{
    ng_panel(s,264,172,988,356,NG_LINE,NG_SURFACE);
    ng_icon(s,720,216,1U,NG_CYAN);
    ng_center(s,288,298,940,title,36,NG_WHITE);
    ng_center(s,288,362,940,detail,26,NG_MUTED);
}
/* The dynamic launch region is entirely inside the flat panel fill.
 * Keep its bounds synchronized with the pixel-equivalence regression. */
enum { NG_LOADING_X=910, NG_LOADING_Y=340,
       NG_LOADING_W=306, NG_LOADING_H=164 };
static void ng_launch_progress(ng_scene_t *s)
{
    const console_shell_loading_info_t *loading=&s->shell->loading;
    char elapsed[32];
    (void)snprintf(elapsed,sizeof(elapsed),"Elapsed %lus",
        (unsigned long)loading->elapsed_seconds);
    ng_label(s,910,340,loading->stage,24,NG_WHITE,306);
    ng_label(s,910,385,loading->detail,21,NG_MUTED,306);
    if(loading->progress_visible){
        const unsigned percent=loading->progress_percent>100U?100U:loading->progress_percent;
        ng_rect(s,910,430,306,14,NG_LINE);
        ng_rect(s,910,430,(int)(306U*percent/100U),14,NG_CYAN);
    }
    ng_label(s,910,474,elapsed,24,NG_MUTED,306);
}
static void ng_launch(ng_scene_t *s)
{
    const console_shell_t *sh=s->shell;
    const console_app_descriptor_t *app=NULL;
    for(size_t i=0;i<sh->app_count;++i)if(sh->apps[i].id==sh->active_app_id){app=&sh->apps[i];break;}
    if(!app){ng_heading(s,"Opening game");ng_empty(s,"Getting ready","");return;}
    const console_shell_loading_info_t *loading=&sh->loading;
    ng_heading(s,loading->active&&loading->title[0]?loading->title:app->title);
    ng_panel(s,264,168,988,448,NG_LINE,NG_SURFACE);
    ng_game_art(s,app,284,188,600,337);
    ng_label(s,910,206,"GET READY",24,NG_CYAN,306);
    char category[CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES];
    ng_category_label(ng_app_category(app),category);
    ng_label(s,910,254,category,30,NG_WHITE,306);
    ng_label(s,288,558,app->subtitle?app->subtitle:"",28,NG_WHITE,932);
    if(loading->active)ng_launch_progress(s);
    else ng_label(s,910,452,"Opening...",28,NG_MUTED,306);
}
static unsigned ng_nav_selection(const console_shell_t *sh)
{
    if(sh->page==CONSOLE_PAGE_EXTERNAL)return 1U;
    if(sh->page==CONSOLE_PAGE_HOME)return sh->home_all_programs?1U:0U;
    if(sh->page==CONSOLE_PAGE_MULTIPLAYER)return 2U;
    if(sh->page==CONSOLE_PAGE_FILES||sh->page==CONSOLE_PAGE_GAMES||
       sh->page==CONSOLE_PAGE_FILE_TRANSFER||sh->page==CONSOLE_PAGE_USB_DRIVE)return 3U;
    if(sh->page==CONSOLE_PAGE_SAVES||sh->page==CONSOLE_PAGE_ACHIEVEMENTS)return 1U;
    return 4U;
}
static void ng_chrome(ng_scene_t *s)
{
    const console_shell_t *sh=s->shell;char value[64];
    if(s->skip_rows){
        /* The immutable raster replaces this whole rectangle; do not dirty
         * it with a redundant CPU clear before the non-overlapping copy. */
        const bool library=sh->page==CONSOLE_PAGE_HOME&&sh->home_all_programs;
        const int top=library?NG_GRID_TOP:NG_FILES_TOP,bottom=library?NG_GRID_BOTTOM:NG_FILES_BOTTOM;
        ng_rect(s,0,0,NG_W,top,NG_INK);ng_rect(s,0,bottom,NG_W,NG_H-bottom,NG_INK);
        ng_rect(s,0,top,264,bottom-top,NG_INK);ng_rect(s,1228,top,NG_W-1228,bottom-top,NG_INK);
    }else ng_rect(s,0,0,NG_W,NG_H,NG_INK);
    ng_rect(s,0,0,240,NG_H,NG_RGB(9,26,38));
    if(s->pixels)for(size_t y=0;y<64U;++y)for(size_t x=0;x<64U;++x){
        const size_t i=(y*64U+x)*3U;const unsigned alpha=(unsigned)ng_brand_mark[i+2U]*15U/255U;
        uint16_t *dst=ng_pixel(s,(int)x+12,(int)y+4);
        if(dst&&alpha)*dst=ng_mix((uint16_t)((unsigned)ng_brand_mark[i]|((unsigned)ng_brand_mark[i+1U]<<8U)),*dst,alpha);
    }
    ng_text(s,83,14,"GameChangersAI",30,NG_WHITE,360);
    const unsigned game_volume=bounded_volume(sh->runtime.game_volume_step);
    ng_add(s,596,4,196,68,NC_AUDIO_STATUS,0,true);
    if(ng_focused(s,NC_AUDIO_STATUS,0))ng_panel(s,596,4,196,68,NG_WHITE,NG_INK);
    ng_icon(s,606,18,5U,game_volume?NG_MUTED:NG_AMBER);
    if(!game_volume)ng_line(s,606,18,644,54,NG_AMBER);
    if(game_volume)(void)snprintf(value,sizeof(value),"Game %u/10",game_volume);
    else (void)snprintf(value,sizeof(value),"Muted");
    ng_label(s,654,22,value,24,game_volume?NG_WHITE:NG_AMBER,138);
    if(sh->runtime.game_storage_space_valid){
        char size[16];p4_format_file_size((uint64_t)sh->runtime.game_storage_free_kib*1024U,size);
        (void)snprintf(value,sizeof(value),"%s free",size);
    } else (void)snprintf(value,sizeof(value),"Storage %s",sh->runtime.game_storage_state==CONSOLE_STORAGE_READY?"ready":"unavailable");
    ng_label(s,814,22,value,24,NG_MUTED,270);
    (void)snprintf(value,sizeof(value),sh->runtime.battery_sample_valid?"%u%%":"--%%",(unsigned)sh->runtime.battery_percent);
    ng_label(s,1130,18,value,30,sh->runtime.battery_sample_valid?NG_WHITE:NG_MUTED,110);
    ng_panel(s,1058,25,51,27,NG_MUTED,NG_INK);ng_rect(s,1109,33,5,12,NG_MUTED);
    if(sh->runtime.battery_sample_valid)ng_rect(s,1063,30,(int)(sh->runtime.battery_percent>100U?100U:sh->runtime.battery_percent)*41/100,17,NG_CYAN);
    ng_add(s,1090,0,172,80,NC_PAGE,CONSOLE_PAGE_POWER,true);
    const char *nav[]={"Home","Games","Multiplayer","Files","Settings"};
    const unsigned selected=ng_nav_selection(sh);
    for(unsigned i=0;i<5U;++i){
        const int y=128+(int)i*94;const bool active=selected==i;
        ng_add(s,12,y,216,82,NC_HOME+i,0,true);
        if(active)ng_round(s,12,y,216,82,12,NG_CYAN);
        if(ng_focused(s,NC_HOME+i,0))ng_panel(s,12,y,216,82,NG_WHITE,active?NG_CYAN:NG_SURFACE);
        ng_icon(s,30,y+20,i,active?NG_INK:NG_MUTED);
        ng_text(s,82,y+23,nav[i],24,active?NG_INK:NG_MUTED,140);
    }
    ng_text(s,20,664,sh->page==CONSOLE_PAGE_EXTERNAL?"Please wait":"A Select  B Back",22,NG_MUTED,220);
    ng_text(s,1020,676,"OS " CONSOLE_PRODUCT_VERSION,22,NG_MUTED,220);
}
static void ng_home(ng_scene_t *s)
{
    console_shell_t *sh=s->shell;size_t games[CONSOLE_SHELL_MAX_APPS];const size_t count=ng_games(sh,games);
    ng_heading(s,sh->home_all_programs?"Games":"Your games");
    if(!count){ng_empty(s,"Your library is ready","Add games through Files or USB transfer.");return;}
    if(!sh->home_all_programs){
        const console_app_descriptor_t *app=&sh->apps[games[0]];
        ng_panel(s,264,150,988,282,NG_CYAN,NG_INK);
        ng_game_art(s,app,268,154,980,274);
        /* Opaque backing keeps any game's title readable, including fallback art. */
        ng_round(s,280,174,395,96,10,NG_INK);
        ng_label(s,296,192,app->title,42,NG_WHITE,370);
        ng_button(s,292,318,250,84,app->enabled?"Play":"Unavailable",NC_LAUNCH,(unsigned)games[0],app->enabled,true);
        for(size_t i=1;i<count&&i<4U;++i)ng_game_card(s,games[i],264+(int)(i-1U)*334,450,320,208);
    } else {
        const size_t filtered=ng_library_games(sh,games);ng_set_scroll(sh,true,sh->ng_library_scroll);
        char category[CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES],value[64];
        ng_category_label(sh->ng_category,category);
        (void)snprintf(value,sizeof(value),"Category: %s",category[0]?category:"All");
        ng_button(s,640,86,424,64,value,NC_FILTER,0,true,false);
        (void)snprintf(value,sizeof(value),"%u titles",(unsigned)filtered);ng_label(s,1090,105,value,24,NG_MUTED,162);
        s->clip_top=NG_GRID_TOP;s->clip_bottom=NG_GRID_BOTTOM;
        ng_library_rows(s,games,filtered);
        s->clip_top=0;s->clip_bottom=0;ng_scrollbar(s,filtered,true);
        ng_label(s,878,643,"Swipe to scroll",22,NG_MUTED,340);
    }
}
static void ng_settings(ng_scene_t *s)
{
    ng_heading(s,"Settings");
    const char *labels[]={"Sound","Appearance","Controllers","Storage","Battery","System"};
    const char *sub[]={"Startup and game volume","Text and motion","Input and mappings","Games and free space","Power and charge status","Clock, sensors and device"};
    const console_page_t pages[]={CONSOLE_PAGE_AUDIO,CONSOLE_PAGE_APPEARANCE,CONSOLE_PAGE_CONTROLLERS,CONSOLE_PAGE_STORAGE,CONSOLE_PAGE_POWER,CONSOLE_PAGE_SYSTEM};
    for(unsigned i=0;i<6U;++i){
        const int x=264+(int)(i%2U)*506,y=158+(int)(i/2U)*166;
        ng_add(s,x,y,482,146,NC_PAGE,(unsigned)pages[i],true);
        ng_panel(s,x,y,482,146,ng_focused(s,NC_PAGE,(unsigned)pages[i])?NG_CYAN:NG_LINE,NG_SURFACE);
        const unsigned icons[]={5U,6U,1U,8U,7U,4U};
        ng_icon(s,x+20,y+27,icons[i],NG_CYAN);
        ng_label(s,x+82,y+24,labels[i],36,NG_WHITE,376);
        ng_label(s,x+82,y+80,sub[i],24,NG_MUTED,376);
    }
}

static void ng_battery(ng_scene_t *s)
{
    const console_shell_runtime_info_t *r=&s->shell->runtime;char value[80];
    ng_heading(s,"Battery");
    ng_panel(s,264,174,360,476,NG_LINE,NG_SURFACE);
    ng_round(s,401,232,76,28,8,NG_CYAN);ng_panel(s,346,256,188,260,NG_CYAN,NG_INK);
    if(r->battery_sample_valid){
        const int fill=(int)(r->battery_percent>100U?100U:r->battery_percent)*224/100;
        ng_round(s,361,501-fill,158,fill,6,NG_CYAN);
    }else ng_center(s,350,333,180,"?",84,NG_AMBER);
    ng_center(s,280,550,328,r->battery_sample_valid?"Voltage estimate":"Reading unavailable",24,NG_MUTED);
    (void)snprintf(value,sizeof(value),r->battery_sample_valid?"%u%%":"--%%",(unsigned)r->battery_percent);
    ng_text(s,666,173,value,68,NG_WHITE,520);
    ng_label(s,668,274,r->battery_sample_valid?"Battery level":"Battery reading unavailable",32,NG_WHITE,550);
    (void)snprintf(value,sizeof(value),"%u.%02u V",(unsigned)r->battery_millivolts/1000U,(unsigned)(r->battery_millivolts%1000U)/10U);
    ng_text(s,668,344,"Pack voltage",26,NG_MUTED,280);ng_text(s,1010,344,r->battery_supported?value:"Unavailable",28,NG_WHITE,220);
    ng_text(s,668,400,"Charge state",26,NG_MUTED,280);
    const char *state=!r->battery_sample_valid||!r->battery_current_valid?"Unknown":r->battery_milliamps < -10?"Charging":r->battery_milliamps>10?"Discharging":"Idle / unknown";
    ng_text(s,1010,400,state,28,NG_WHITE,220);
    if(r->battery_current_valid&&r->battery_sample_valid){
        (void)snprintf(value,sizeof(value),"%ld mA",(long)r->battery_milliamps);
        ng_text(s,668,456,"Measured current",26,NG_MUTED,320);ng_text(s,1010,456,value,28,NG_WHITE,220);
    }else ng_label(s,668,462,"Check the battery connection.",28,NG_AMBER,550);
    ng_label(s,668,543,r->battery_sample_valid?"Percentage is estimated from voltage.":"Charging status is not verified.",25,NG_MUTED,550);
    ng_label(s,668,591,"Updates automatically",24,NG_MUTED,550);
}
static void ng_audio(ng_scene_t *s)
{
    ng_heading(s,"Sound");char value[32];
    const uint8_t steps[]={s->shell->runtime.boot_volume_step,s->shell->runtime.game_volume_step};
    const char *labels[]={"Startup sound","Game volume"};
    for(unsigned i=0;i<2U;++i){
        const int y=190+(int)i*198;ng_panel(s,264,y,988,174,NG_LINE,NG_SURFACE);
        ng_label(s,290,y+18,labels[i],34,NG_WHITE,540);
        ng_rect(s,296,y+108,476,10,NG_LINE);ng_rect(s,296,y+108,(int)bounded_volume(steps[i])*476/10,10,NG_CYAN);
        ng_button(s,824,y+45,88,88,"-",NC_AUDIO,i*2U,steps[i]>0U,false);
        if(steps[i])(void)snprintf(value,sizeof(value),"%u / 10",(unsigned)bounded_volume(steps[i]));
        else (void)snprintf(value,sizeof(value),"Muted");
        ng_center(s,918,y+65,210,value,36,NG_WHITE);
        ng_button(s,1132,y+45,88,88,"+",NC_AUDIO,i*2U+1U,steps[i]<10U,true);
    }
    ng_label(s,282,615,s->shell->runtime.audio_settings_persistent?"Volume settings are saved.":"Volume settings apply to this session.",26,NG_MUTED,950);
}
static void ng_appearance(ng_scene_t *s)
{
    ng_heading(s,"Appearance");
    ng_panel(s,264,178,988,158,NG_LINE,NG_SURFACE);
    ng_text(s,290,200,"Large text",34,NG_WHITE,560);
    ng_text(s,290,255,"Larger controls and list labels",25,NG_MUTED,600);
    ng_button(s,1040,214,176,88,s->shell->ng_large_text?"On":"Off",NC_APPEARANCE,0,true,s->shell->ng_large_text);
    ng_panel(s,264,354,988,158,NG_LINE,NG_SURFACE);
    ng_text(s,290,376,"Reduce motion",34,NG_WHITE,560);
    ng_text(s,290,431,"Keep navigation feedback still",25,NG_MUTED,600);
    ng_button(s,1040,390,176,88,s->shell->ng_reduce_motion?"On":"Off",NC_APPEARANCE,1,true,s->shell->ng_reduce_motion);
    ng_button(s,264,548,472,88,"Display test",NC_PAGE,CONSOLE_PAGE_COLORS,true,false);
    ng_button(s,760,548,492,88,"Touch test",NC_PAGE,CONSOLE_PAGE_TOUCH,true,false);
    ng_text(s,280,658,"Appearance settings apply to this session.",22,NG_MUTED,780);
}
static const char *ng_storage_name(console_shell_storage_state_t state)
{
    switch(state){
    case CONSOLE_STORAGE_READY:return "Ready";
    case CONSOLE_STORAGE_MISSING:return "No card";
    case CONSOLE_STORAGE_INVALID:return "Check contents";
    case CONSOLE_STORAGE_FORMAT_REQUIRED:return "Format required";
    case CONSOLE_STORAGE_FAULT:return "Storage fault";
    default:return "Unavailable";
    }
}
static void ng_storage(ng_scene_t *s)
{
    const console_shell_runtime_info_t *r=&s->shell->runtime;char value[80];
    ng_heading(s,"Storage");
    ng_panel(s,264,178,988,338,NG_LINE,NG_SURFACE);
    ng_status_row(s,194,r->sd_card_storage?"SD card":"Game storage",ng_storage_name(r->game_storage_state),NG_CYAN);
    if(r->game_storage_space_valid){char available[16],total[16];p4_format_file_size((uint64_t)r->game_storage_free_kib*1024U,available);p4_format_file_size((uint64_t)r->game_storage_kib*1024U,total);(void)snprintf(value,sizeof(value),"%s / %s",available,total);}
    else (void)snprintf(value,sizeof(value),"Unavailable");
    ng_status_row(s,269,"Free / total",value,NG_WHITE);
    (void)snprintf(value,sizeof(value),"%u cartridges + %u built-in",(unsigned)r->valid_cart_count,(unsigned)r->builtin_game_count);
    ng_status_row(s,344,"Installed games",value,NG_WHITE);
    ng_label(s,290,438,r->game_storage_operation!=CONSOLE_STORAGE_OPERATION_NONE?"Storage operation in progress...":
        r->game_storage_last_check_ok?"Last check passed":"Use Check card to inspect storage.",26,NG_MUTED,920);
    ng_button(s,264,548,304,88,"Check card",NC_STORAGE,0,storage_check_enabled(s->shell),false);
    ng_button(s,606,548,304,88,"Retry mount",NC_STORAGE,1,storage_retry_enabled(s->shell),false);
    ng_button(s,948,548,304,88,"Repair card",NC_STORAGE,2,storage_repair_enabled(s->shell),false);
}
static void ng_file_scroll_buttons(ng_scene_t *s)
{
    const console_shell_t *sh=s->shell;
    /* This exact rectangle is also the stationary patch on endpoint changes.
     * Restore the INK gap between the two buttons as well as both controls. */
    ng_rect(s,368,558,288,88,NG_INK);
    ng_button(s,368,558,136,88,"Prev",NC_FILE_PREV,0,sh->ng_file_scroll>0,false);
    ng_button(s,520,558,136,88,"Next",NC_FILE_NEXT,0,
        sh->ng_file_scroll<ng_scroll_max(sh->files.entry_count,false),false);
}
static void ng_files(ng_scene_t *s)
{
    console_shell_t *sh=s->shell;const bool games=sh->page==CONSOLE_PAGE_GAMES;
    ng_heading(s,games?"Manage games":"Files");
    ng_label(s,280,137,sh->files.path_label,24,NG_MUTED,282);
    const size_t count=sh->files.entry_count;ng_set_scroll(sh,false,sh->ng_file_scroll);
    if(!sh->files.available)ng_label(s,292,210,"Storage is unavailable.",34,NG_WHITE,900);
    else if(!count)ng_label(s,292,210,"This folder is empty.",34,NG_WHITE,900);
    s->clip_top=NG_FILES_TOP;s->clip_bottom=NG_FILES_BOTTOM;
    ng_file_rows(s,count);
    s->clip_top=0;s->clip_bottom=0;ng_scrollbar(s,count,false);
    const bool selected=sh->file_selected_index<count;
    ng_button(s,264,558,88,88,"Up",NC_FILE_OPEN,1,sh->files.can_go_up,false);
    ng_file_scroll_buttons(s);
    ng_button(s,672,558,176,88,"Refresh",NC_REFRESH,0,true,false);
    ng_button(s,864,558,304,88,file_selected_is_installable(sh)?"Install update":"Open",NC_FILE_OPEN,0,selected&&(file_selected_is_openable(sh)||file_selected_is_installable(sh)),true);
    ng_button(s,1196,574,56,56,"...",NC_FILE_MENU,0,selected&&file_selected_is_removable(sh),false);
    const char *notice=sh->file_notice==CONSOLE_FILE_NOTICE_ERROR?"Operation failed":
        sh->file_notice==CONSOLE_FILE_NOTICE_DELETED?"Deleted":
        sh->file_notice==CONSOLE_FILE_NOTICE_UPDATING?"Installing update...":
        sh->file_notice==CONSOLE_FILE_NOTICE_REFRESHED?"Refreshed":"";
    ng_label(s,282,658,notice,22,NG_MUTED,700);
}
static void ng_system(ng_scene_t *s)
{
    const console_shell_runtime_info_t *r=&s->shell->runtime;char value[80];
    ng_heading(s,"System");ng_panel(s,264,178,988,260,NG_LINE,NG_SURFACE);
    ng_status_row(s,188,"GameChangersAI OS",CONSOLE_PRODUCT_VERSION,NG_WHITE);
    ng_status_row(s,248,"Device","M5Stack Tab5",NG_WHITE);
    ng_status_row(s,308,"Display","1280 x 720",NG_WHITE);
    ng_status_row(s,368,"Clock (UTC)",r->rtc_valid?r->rtc_datetime:"Sync from connected computer",r->rtc_valid?NG_WHITE:NG_MUTED);
    const char *labels[]={"Sensors","Terminal","Touch test","Display test"};
    const console_page_t pages[]={CONSOLE_PAGE_SENSORS,CONSOLE_PAGE_TERMINAL,CONSOLE_PAGE_TOUCH,CONSOLE_PAGE_COLORS};
    for(unsigned i=0;i<4U;++i)ng_button(s,264+(int)i*253,458,229,72,labels[i],NC_PAGE,(unsigned)pages[i],true,false);
    /* Installed utilities remain reachable without appearing in the game library. */
    unsigned utility=0U;
    for(size_t i=0;i<s->shell->app_count&&utility<3U;++i)if(ng_system_utility(&s->shell->apps[i])){
        const console_app_descriptor_t *app=&s->shell->apps[i];
        ng_button(s,264+(int)utility*334,550,320,72,app->title,NC_LAUNCH,(unsigned)i,app->enabled,false);
        ++utility;
    }
    (void)snprintf(value,sizeof(value),"Uptime %lu s   /   RAM %lu KB   /   PSRAM %lu KB",
        (unsigned long)r->uptime_seconds,(unsigned long)r->internal_free_kib,(unsigned long)r->psram_free_kib);
    ng_label(s,282,642,value,24,NG_MUTED,944);
}
static void ng_sensors(ng_scene_t *s)
{
    const console_shell_runtime_info_t *r=&s->shell->runtime;char value[80];
    ng_heading(s,"Sensors");ng_panel(s,264,178,988,430,NG_LINE,NG_SURFACE);
    if(r->motion_valid)(void)snprintf(value,sizeof(value),"%ld  %ld  %ld mg",(long)r->accel_mg[0],(long)r->accel_mg[1],(long)r->accel_mg[2]);
    else (void)snprintf(value,sizeof(value),"Unavailable");
    ng_status_row(s,202,"Acceleration X / Y / Z",value,NG_WHITE);
    if(r->motion_valid)(void)snprintf(value,sizeof(value),"%ld  %ld  %ld mdps",(long)r->gyro_mdps[0],(long)r->gyro_mdps[1],(long)r->gyro_mdps[2]);
    else (void)snprintf(value,sizeof(value),"Unavailable");
    ng_status_row(s,285,"Rotation X / Y / Z",value,NG_WHITE);
    if(r->temperature_valid)(void)snprintf(value,sizeof(value),"%ld C",(long)(r->temperature_millicelsius/1000));
    else (void)snprintf(value,sizeof(value),"Unavailable");
    ng_status_row(s,368,"Sensor temperature",value,NG_WHITE);
    ng_status_row(s,451,"Clock (UTC)",r->rtc_valid?r->rtc_datetime:"Not set",NG_WHITE);
    ng_label(s,290,545,"Temperature is the sensor die, not the room.",24,NG_MUTED,916);
    ng_label(s,282,640,"Readings update automatically.",26,NG_MUTED,916);
}
static void ng_controllers(ng_scene_t *s)
{
    const console_shell_runtime_info_t *r=&s->shell->runtime;
    ng_heading(s,"Controllers");
    ng_panel(s,264,178,988,270,NG_LINE,NG_SURFACE);
    ng_label(s,290,208,r->controller_ready?"Controller connected":"No controller connected",40,NG_WHITE,926);
    ng_label(s,290,270,r->controller_ready?controller_source_name((uint8_t)r->controller_transport):"Connect a wired controller to USB-A.",28,NG_MUTED,926);
    ng_label(s,290,324,r->controller_mapping_active?controller_mapping_target_name(r->controller_mapping_target):"Touch controls are ready.",30,NG_CYAN,926);
    ng_label(s,290,382,r->ble_controller_supported?"Bluetooth controller controls below.":"Bluetooth unavailable in this firmware.",25,NG_MUTED,926);
    const char *labels[]={r->ble_controller_enabled?"Disable BLE":"Enable BLE","Pair","Disconnect",
        r->controller_mapping_active?"Cancel mapping":"Map buttons","Reset mapping","Forget bond"};
    for(unsigned i=0;i<6U;++i)ng_button(s,264+(int)(i%3U)*334,470+(int)(i/3U)*98,320,84,
        labels[i],NC_CONTROLLER,i,controller_action_enabled(s->shell,i),i==3U&&r->controller_ready);
}
static void ng_transfer(ng_scene_t *s)
{
    const console_shell_runtime_info_t *r=&s->shell->runtime;char value[80];
    ng_heading(s,"File transfer");ng_panel(s,264,178,988,388,NG_LINE,NG_SURFACE);
    ng_label(s,296,204,r->file_transfer_ready?"USB transfer ready":"USB transfer unavailable",38,NG_WHITE,916);
    ng_label(s,296,265,"Use the connected computer to send files.",28,NG_MUTED,916);
    ng_label(s,296,331,r->file_transfer_name[0]?r->file_transfer_name:"Waiting for a transfer",30,NG_CYAN,916);
    (void)snprintf(value,sizeof(value),"%s   %u%%",file_transfer_state_name(r->file_transfer_state),(unsigned)r->file_transfer_progress_percent);
    ng_label(s,296,390,value,28,NG_WHITE,916);
    ng_rect(s,296,465,916,14,NG_LINE);
    unsigned progress=r->file_transfer_progress_percent>100U?100U:r->file_transfer_progress_percent;
    ng_rect(s,296,465,(int)progress*916/100,14,NG_CYAN);
    ng_button(s,264,588,304,80,"Files",NC_FILES,0,true,false);
    ng_button(s,606,588,304,80,"Manage games",NC_PAGE,CONSOLE_PAGE_GAMES,true,false);
    ng_button(s,948,588,304,80,"USB drive",NC_PAGE,CONSOLE_PAGE_USB_DRIVE,true,false);
}
static void ng_usb(ng_scene_t *s)
{
    const console_shell_runtime_info_t *r=&s->shell->runtime;
    ng_heading(s,"USB drive");ng_empty(s,r->usb_storage_supported?
        (r->usb_drive_active?"USB drive active":"USB drive ready"):"USB drive unavailable",
        r->usb_storage_supported?"Eject the drive on your computer before returning.":"Use USB file transfer on this Tab5 firmware.");
    ng_button(s,264,560,474,88,r->usb_drive_active?"Return to console":"Enable USB drive",NC_USB,0,system_usb_button_enabled(s->shell),true);
    ng_button(s,778,560,474,88,"File transfer",NC_PAGE,CONSOLE_PAGE_FILE_TRANSFER,true,false);
}
static void ng_lists(ng_scene_t *s)
{
    console_shell_t *sh=s->shell;const bool saves=sh->page==CONSOLE_PAGE_SAVES;
    const size_t count=saves?sh->saves.count:sh->achievements.count;char value[80];
    ng_heading(s,saves?"Saved games":"Achievements");
    const size_t first=sh->ng_list_first<count?sh->ng_list_first:0U;
    if(!count)ng_empty(s,saves?"No saved games yet":"No achievements yet",saves?"Games save progress through the OS.":"Play a game to start earning achievements.");
    for(size_t i=first;i<count&&i<first+4U;++i){
        const int y=178+(int)(i-first)*92;ng_panel(s,264,y,988,84,NG_LINE,NG_SURFACE);
        const char *title,*description;
        if(saves){title=sh->saves.slots[i].slot_name;description=sh->saves.slots[i].game_id;}
        else {const p4_achievement_entry_t *e=p4_achievement_catalog_get(&sh->achievements,i);if(!e)continue;title=e->title;description=e->description;}
        ng_label(s,290,y+7,title,30,NG_WHITE,916);ng_label(s,290,y+46,description,24,NG_MUTED,916);
    }
    ng_button(s,264,574,224,80,"Previous",NC_LIST_PREV,0,first>0U,false);
    (void)snprintf(value,sizeof(value),"%u %s",(unsigned)count,saves?"saved slots":"unlocked this session");
    ng_center(s,508,590,480,value,28,NG_MUTED);
    ng_button(s,1028,574,224,80,"Next",NC_LIST_NEXT,0,first+4U<count,false);
}
static void ng_display_test(ng_scene_t *s)
{
    ng_heading(s,"Display test");
    const uint16_t colors[]={0xffff,0xffe0,0x07ff,0x07e0,0xf81f,0xf800,0x001f};
    for(int i=0;i<7;++i)ng_rect(s,264+i*141,180,141,268,colors[i]);
    for(int i=0;i<16;++i){const unsigned v=(unsigned)i*17U;ng_rect(s,264+i*61,468,61,74,NG_RGB(v,v,v));}
    ng_text(s,282,567,"Aa 0123456789",32,NG_WHITE,470);
    ng_text(s,760,567,"Readable secondary text",26,NG_MUTED,470);
    ng_button(s,264,616,474,80,"Touch test",NC_PAGE,CONSOLE_PAGE_TOUCH,true,false);
    ng_button(s,778,616,474,80,"Done",NC_SETTINGS,0,true,true);
}
static void ng_touch_test(ng_scene_t *s)
{
    ng_heading(s,"Touch test");ng_panel(s,264,178,988,440,NG_LINE,NG_SURFACE);
    ng_text(s,290,201,"Touch anywhere in the test area",32,NG_WHITE,916);
    ng_text(s,290,253,"Each contact shows its real screen coordinates.",25,NG_MUTED,916);
    for(size_t i=0;i<s->shell->contact_count;++i){
        const console_shell_contact_t *c=&s->shell->contacts[i];char value[48];
        const int x=(int)c->x,y=(int)c->y;
        ng_round(s,x-15,y-15,30,30,15,NG_CYAN);
        (void)snprintf(value,sizeof(value),"X %u  Y %u",(unsigned)c->x,(unsigned)c->y);
        ng_label(s,290,322+(int)i*48,value,30,NG_WHITE,916);
    }
    ng_button(s,264,630,988,80,"Done",NC_SETTINGS,0,true,true);
}
static void ng_terminal(ng_scene_t *s)
{
    ng_heading(s,"Terminal");
    char value[P4_TERMINAL_INPUT_BYTES+4];(void)snprintf(value,sizeof(value),"> %s",s->shell->terminal.input);
    if(!s->shell->ng_keyboard){
        ng_panel(s,264,178,988,330,NG_LINE,NG_SURFACE);
        for(size_t i=0;i<s->shell->terminal.line_count;++i)
            ng_label(s,290,205+(int)i*53,s->shell->terminal.lines[i],28,NG_MUTED,930);
        ng_panel(s,264,526,988,80,NG_CYAN,NG_INK);ng_label(s,286,547,value,30,NG_WHITE,942);
        ng_button(s,264,624,988,80,"On-screen keyboard",NC_KEYBOARD,0,true,true);
        return;
    }
    ng_panel(s,264,158,988,76,NG_CYAN,NG_INK);ng_label(s,286,177,value,30,NG_WHITE,942);
    static const char *rows[]={"1234567890","QWERTYUIOP","ASDFGHJKL","ZXCVBNM./-"};
    for(unsigned row=0;row<4U;++row)for(unsigned col=0;rows[row][col];++col){
        const char key=rows[row][col];const int x=264+(int)col*98+(row==2U?36:0);
        char label[2]={key,0};
        ng_button(s,x,250+(int)row*84,90,76,label,NC_KEY,(unsigned char)key,true,false);
    }
    ng_button(s,264,600,340,72,"Space",NC_KEY,' ',true,false);
    ng_button(s,620,600,284,72,"Backspace",NC_KEY,8,true,false);
    ng_button(s,920,600,332,72,"Enter",NC_KEY,13,true,true);
}

static void ng_mp_value(const console_shell_runtime_info_t *r,unsigned option,char *value,size_t bytes)
{
    switch(option){
    case CONSOLE_MULTIPLAYER_OPTION_GAME:(void)snprintf(value,bytes,"%s",r->multiplayer_game_title);break;
    case CONSOLE_MULTIPLAYER_OPTION_MODE:(void)snprintf(value,bytes,"%s",multiplayer_mode_name(r->multiplayer_game_mode));break;
    case CONSOLE_MULTIPLAYER_OPTION_MAP:(void)snprintf(value,bytes,"Episode %u / Map %u",(unsigned)r->multiplayer_episode,(unsigned)r->multiplayer_map);break;
    case CONSOLE_MULTIPLAYER_OPTION_SKILL:(void)snprintf(value,bytes,"%s",multiplayer_skill_name(r->multiplayer_skill));break;
    case CONSOLE_MULTIPLAYER_OPTION_MONSTERS:(void)snprintf(value,bytes,"%s",r->multiplayer_no_monsters?"Off":"On");break;
    case CONSOLE_MULTIPLAYER_OPTION_FAST:(void)snprintf(value,bytes,"%s",r->multiplayer_fast_monsters?"On":"Off");break;
    case CONSOLE_MULTIPLAYER_OPTION_RESPAWN:(void)snprintf(value,bytes,"%s",r->multiplayer_respawn_monsters?"On":"Off");break;
    case CONSOLE_MULTIPLAYER_OPTION_TIME_LIMIT:(void)snprintf(value,bytes,"%u minutes",(unsigned)r->multiplayer_time_limit_minutes);break;
    case CONSOLE_MULTIPLAYER_OPTION_TRANSPORT:(void)snprintf(value,bytes,"%s",r->multiplayer_transport_kind==2U?"Local Wi-Fi":r->multiplayer_transport_kind==1U?"Bluetooth":"USB / wired relay");break;
    case CONSOLE_MULTIPLAYER_OPTION_DICE:(void)snprintf(value,bytes,"%s",!r->multiplayer_dice_available?"Unavailable":r->multiplayer_dice_enabled?"On":"Off");break;
    default:(void)snprintf(value,bytes,"%u rooms",(unsigned)r->multiplayer_lobby_count);break;
    }
}
static const console_multiplayer_game_display_t *ng_mp_game(const console_shell_runtime_info_t *r)
{
    return r->multiplayer_game_selection<r->multiplayer_game_count&&
        r->multiplayer_game_selection<CONSOLE_MULTIPLAYER_MAX_GAMES
        ?&r->multiplayer_games[r->multiplayer_game_selection]:NULL;
}
static size_t ng_mp_settings(const console_shell_runtime_info_t *r,unsigned *options)
{
    if(r->multiplayer_game_is_arena){options[0]=CONSOLE_MULTIPLAYER_OPTION_MAP;return 1U;}
    if(r->multiplayer_game_is_doom){
        for(unsigned i=0;i<7U;++i)options[i]=CONSOLE_MULTIPLAYER_OPTION_MODE+i;
        return 7U;
    }
    if(r->multiplayer_dice_available){options[0]=CONSOLE_MULTIPLAYER_OPTION_DICE;return 1U;}
    return 0U;
}
static void ng_mp_status(ng_scene_t *s)
{
    const char *status=s->shell->runtime.multiplayer_status;
    if(status[0])ng_label(s,282,571,status,22,NG_AMBER,950);
}
enum { NG_MP_GAMES_PER_PAGE=12U };
static size_t ng_list_page_size(const console_shell_t *sh)
{
    return sh->page==CONSOLE_PAGE_MULTIPLAYER&&sh->multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_GAME
        ?NG_MP_GAMES_PER_PAGE:4U;
}
static void ng_multiplayer(ng_scene_t *s)
{
    console_shell_t *sh=s->shell;const console_shell_runtime_info_t *r=&sh->runtime;
    const console_multiplayer_game_display_t *game=ng_mp_game(r);
    const bool choose_game=sh->multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_GAME;
    const bool choose_transport=sh->multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_TRANSPORT;
    const bool joining=sh->multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_JOIN;
    const bool busy=r->multiplayer_launch_syncing||r->multiplayer_transport_starting;
    const bool editable=r->multiplayer_settings_editable&&!busy;
    ng_heading(s,choose_game?"Choose a game":choose_transport?"Choose a connection":
        sh->multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_ROLE?"Host or join":
        joining?"Join game":sh->multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS?"Match settings":"Host game");
    ng_label(s,282,178,choose_game?"Step 1 of 3: Game":
        choose_transport?"Step 2 of 3: Connection":
        "Step 3 of 3: Host or join",24,NG_CYAN,940);
    if(choose_game){
        const size_t count=r->multiplayer_game_count<CONSOLE_MULTIPLAYER_MAX_GAMES?
            r->multiplayer_game_count:CONSOLE_MULTIPLAYER_MAX_GAMES;
        const size_t first=sh->ng_list_first<count?sh->ng_list_first:0U;
        char position[64];
        (void)snprintf(position,sizeof(position),"%u titles  /  Page %u of %u",(unsigned)count,
            (unsigned)(first/NG_MP_GAMES_PER_PAGE+1U),
            (unsigned)((count+NG_MP_GAMES_PER_PAGE-1U)/NG_MP_GAMES_PER_PAGE));
        if(count)ng_label(s,882,178,position,22,NG_MUTED,354);
        for(size_t i=first;i<count&&i<first+NG_MP_GAMES_PER_PAGE;++i){
            const console_multiplayer_game_display_t *entry=&r->multiplayer_games[i];
            char label[96];(void)snprintf(label,sizeof(label),"%s%s",entry->title,
                entry->available?"":"  - unavailable");
            ng_button(s,264+(int)((i-first)%2U)*506,228+(int)((i-first)/2U)*56,482,52,label,NC_MP_GAME,(unsigned)i,
                r->multiplayer_core_ready&&editable&&entry->available&&entry->transport_mask,
                i==r->multiplayer_game_selection);
        }
        if(!count)ng_empty(s,"No multiplayer games","Install a multiplayer game to get started.");
        ng_mp_status(s);
        if(count>NG_MP_GAMES_PER_PAGE){
            ng_button(s,264,606,474,64,"Previous games",NC_LIST_PREV,0,first>0U,false);
            ng_button(s,778,606,474,64,"More games",NC_LIST_NEXT,0,first+NG_MP_GAMES_PER_PAGE<count,false);
        }else if(count){
            ng_label(s,282,616,"Choose a game to see its supported connections.",26,NG_MUTED,950);
        }
        return;
    }
    char transport[80];ng_mp_value(r,CONSOLE_MULTIPLAYER_OPTION_TRANSPORT,transport,sizeof(transport));
    ng_panel(s,264,216,988,84,NG_LINE,NG_SURFACE);
    ng_label(s,282,225,game&&game->title[0]?game->title:r->multiplayer_game_title,30,NG_WHITE,946);
    ng_label(s,282,265,choose_transport?"Choose the same connection on both consoles.":transport,22,NG_CYAN,946);
    if(choose_transport){
        const char *names[]={"USB / wired relay","Bluetooth","Local Wi-Fi"};
        for(unsigned i=0;i<3U;++i){
            const bool supported=game&&game->available&&(game->transport_mask&(1U<<i));
            ng_button(s,264,330+(int)i*82,988,70,names[i],NC_MP_TRANSPORT,i,
                editable&&supported,i==r->multiplayer_transport_kind);
        }
        ng_mp_status(s);return;
    }
    if(sh->multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_ROLE){
        const bool ready=r->multiplayer_core_ready&&r->multiplayer_game_ready&&r->multiplayer_transport_ready&&!busy;
        ng_button(s,264,330,474,150,"Host game",NC_MP_VIEW,CONSOLE_MULTIPLAYER_VIEW_HOST,ready,true);
        ng_button(s,778,330,474,150,"Join game",NC_MP_VIEW,CONSOLE_MULTIPLAYER_VIEW_JOIN,ready,false);
        ng_label(s,282,507,"Host creates a room. Join finds a room for this game.",26,NG_MUTED,942);
        ng_mp_status(s);return;
    }
    if(sh->multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS){
        const char *names[]={"Game","Mode","Map","Difficulty","Monsters","Fast monsters","Respawn","Time limit","Transport","Rooms","Dice"};
        unsigned options[7];const size_t count=ng_mp_settings(r,options);
        const size_t first=sh->ng_list_first<count?sh->ng_list_first:0U;
        for(size_t i=first;i<count&&i<first+4U;++i){
            const unsigned option=options[i];const int y=318+(int)(i-first)*62;
            char value[80];ng_mp_value(r,option,value,sizeof(value));
            ng_label(s,282,y+2,names[option],22,NG_MUTED,300);
            ng_label(s,590,y+12,value,26,NG_WHITE,416);
            ng_button(s,1036,y,88,56,"-",NC_MP_OPTION,option*2U,editable,false);
            ng_button(s,1160,y,88,56,"+",NC_MP_OPTION,option*2U+1U,editable,true);
        }
        if(!count)ng_label(s,282,350,"This game has no additional match settings.",28,NG_MUTED,942);
        ng_mp_status(s);
        ng_button(s,264,606,304,64,"Previous",NC_LIST_PREV,0,first>0U,false);
        ng_button(s,606,606,304,64,"Next",NC_LIST_NEXT,0,first+4U<count,false);
        ng_button(s,948,606,304,64,"Done",NC_MP_VIEW,CONSOLE_MULTIPLAYER_VIEW_HOST,true,true);
        return;
    }
    const bool connected=r->multiplayer_lobby_phase==CONSOLE_MULTIPLAYER_LOBBY_CONNECTED;
    const bool connecting=r->multiplayer_lobby_phase==CONSOLE_MULTIPLAYER_LOBBY_JOINING;
    if(joining&&!connecting&&!connected&&!r->multiplayer_launch_syncing){
        if(!r->multiplayer_lobby_count){
            ng_label(s,282,366,"No rooms found",36,NG_WHITE,942);
            ng_label(s,282,420,r->multiplayer_lobby_scanning?"Looking for a room for this game...":
                "Ask the host to create a room with this connection.",26,NG_MUTED,942);
        }
        for(unsigned i=0;i<r->multiplayer_lobby_count&&i<CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX;++i){
            const console_multiplayer_lobby_display_t *room=&r->multiplayer_lobbies[i];char label[96];
            (void)snprintf(label,sizeof(label),"Room %04lX   %u/%u players%s",
                (unsigned long)(room->session_id&UINT32_C(0xffff)),
                (unsigned)room->players_present,(unsigned)room->player_capacity,
                room->game_available?"":"  - incompatible");
            ng_button(s,264,318+(int)i*60,988,54,label,NC_MP_ROOM,i+1U,
                editable&&room->game_available,r->multiplayer_lobby_selection==i+1U);
        }
    }else {
        const char *headline=r->multiplayer_launch_syncing?"Starting game...":
            connecting?"Connecting to host...":connected?"Room connected":
            r->multiplayer_lobby_phase==CONSOLE_MULTIPLAYER_LOBBY_HOSTING?"Room open":"Ready to host";
        const char *detail=r->multiplayer_launch_syncing?"Synchronizing the selected game on every console.":
            connecting?"Keep both consoles here while the connection completes.":
            joining?"Waiting for the host to start the game.":
            r->multiplayer_can_start?"Players are ready. Start when everyone has joined.":
            r->multiplayer_lobby_phase==CONSOLE_MULTIPLAYER_LOBBY_HOSTING?"Waiting for players to join this room.":
            "Review match settings, then create your room.";
        ng_panel(s,264,318,988,162,NG_LINE,NG_SURFACE);
        ng_label(s,282,340,headline,36,NG_WHITE,942);
        ng_label(s,282,407,detail,26,NG_MUTED,942);
        if(!joining)ng_button(s,264,494,988,64,"Match settings",NC_MP_OPTIONS,0,editable,false);
    }
    ng_mp_status(s);
    const bool enabled=multiplayer_primary_action(sh).type!=CONSOLE_ACTION_NONE;
    const char *primary=r->multiplayer_launch_syncing?"Starting game...":
        joining?(connected?"Waiting for host":connecting?"Connecting...":"Join selected room"):
        r->multiplayer_can_start?"Start game":
        r->multiplayer_lobby_phase==CONSOLE_MULTIPLAYER_LOBBY_HOSTING?"Waiting for players":"Create room";
    ng_button(s,264,606,988,64,primary,NC_MP_PRIMARY,0,enabled,true);
}
static void ng_modal(ng_scene_t *s)
{
    console_shell_t *sh=s->shell;const bool repair=sh->storage_repair_confirm;
    s->count=0;
    if(s->pixels)for(int y=0;y<NG_H;++y)for(int x=0;x<NG_W;++x){
        uint16_t *p=s->pixels+(size_t)y*s->stride+(size_t)x;*p=ng_mix(NG_INK,*p,10);}
    ng_panel(s,346,194,838,396,NG_CYAN,NG_SURFACE);
    const bool update=!repair&&file_selected_is_installable(sh);
    ng_center(s,368,230,790,repair?"Repair this card?":update?"Install this update?":"Delete this file?",42,NG_WHITE);
    const char *name=repair?"An existing volume may be modified.":
        sh->file_selected_index<sh->files.entry_count?sh->files.entries[sh->file_selected_index].label:"Selection unavailable";
    ng_center(s,380,307,766,name,32,NG_MUTED);
    ng_center(s,380,366,766,repair?"Check card first if you are unsure.":update?"The console will restart after installation.":"This removes the selected file from storage.",24,NG_MUTED);
    ng_button(s,382,460,358,96,"Cancel",NC_CANCEL,0,true,true);
    ng_button(s,774,460,374,96,repair?"Repair":update?"Install":"Delete",NC_CONFIRM,0,true,false);
}
static void ng_dimmer(ng_scene_t *s)
{
    s->count=0;
    if(s->pixels)for(int y=0;y<NG_H;++y)for(int x=0;x<NG_W;++x){
        uint16_t *p=s->pixels+(size_t)y*s->stride+(size_t)x;*p=ng_mix(NG_INK,*p,10);}
    ng_add(s,0,0,NG_W,NG_H,NC_DISMISS,0,true);
}
static void ng_category_sheet(ng_scene_t *s)
{
    console_shell_t *sh=s->shell;const char *paths[CONSOLE_SHELL_MAX_APPS+1];
    const size_t count=ng_categories(sh,paths);if(sh->ng_category_first>=count)sh->ng_category_first=0;
    ng_dimmer(s);ng_panel(s,304,148,900,502,NG_CYAN,NG_SURFACE);
    ng_label(s,332,170,"Game categories",36,NG_WHITE,660);
    ng_button(s,1116,164,64,56,"X",NC_CANCEL,0,true,false);
    for(size_t i=sh->ng_category_first;i<count&&i<sh->ng_category_first+12U;++i){
        char label[CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES];ng_category_label(paths[i],label);
        const size_t cell=i-sh->ng_category_first;
        ng_button(s,328+(int)(cell%3U)*284,238+(int)(cell/3U)*72,268,60,
            i?label:"All categories",NC_CATEGORY,(unsigned)i,true,strcmp(sh->ng_category,paths[i])==0);}
    if(count>12U){ng_button(s,328,562,268,64,"Previous",NC_CATEGORY_PREV,0,sh->ng_category_first>0U,false);
        ng_button(s,896,562,268,64,"Next",NC_CATEGORY_NEXT,0,sh->ng_category_first+12U<count,false);}
}
static void ng_file_menu(ng_scene_t *s)
{
    console_shell_t *sh=s->shell;
    if(sh->file_selected_index>=sh->files.entry_count||sh->ng_confirm_revision!=sh->files.revision||
       sh->ng_confirm_source!=sh->files.entries[sh->file_selected_index].source_index){sh->ng_file_menu=false;return;}
    ng_dimmer(s);ng_panel(s,898,394,354,158,NG_LINE,NG_SURFACE);
    ng_label(s,918,412,"File actions",28,NG_WHITE,234);
    ng_button(s,1184,402,56,56,"X",NC_CANCEL,0,true,false);
    ng_add(s,914,472,314,60,NC_DELETE,0,file_selected_is_removable(sh));
    if(ng_focused(s,NC_DELETE,0))ng_round(s,914,472,314,60,8,NG_LINE);
    ng_label(s,934,489,sh->page==CONSOLE_PAGE_GAMES?"Remove game...":"Delete file...",26,NG_AMBER,282);
}
static void ng_build_scene(ng_scene_t *s)
{
    ng_chrome(s);
    switch(s->shell->page){
    case CONSOLE_PAGE_HOME:ng_home(s);break;
    case CONSOLE_PAGE_CONTROL_PANEL:ng_settings(s);break;
    case CONSOLE_PAGE_APPEARANCE:ng_appearance(s);break;
    case CONSOLE_PAGE_POWER:ng_battery(s);break;
    case CONSOLE_PAGE_AUDIO:ng_audio(s);break;
    case CONSOLE_PAGE_SYSTEM:ng_system(s);break;
    case CONSOLE_PAGE_SENSORS:ng_sensors(s);break;
    case CONSOLE_PAGE_STORAGE:ng_storage(s);break;
    case CONSOLE_PAGE_CONTROLLERS:ng_controllers(s);break;
    case CONSOLE_PAGE_FILES:case CONSOLE_PAGE_GAMES:ng_files(s);break;
    case CONSOLE_PAGE_FILE_TRANSFER:ng_transfer(s);break;
    case CONSOLE_PAGE_USB_DRIVE:ng_usb(s);break;
    case CONSOLE_PAGE_SAVES:case CONSOLE_PAGE_ACHIEVEMENTS:ng_lists(s);break;
    case CONSOLE_PAGE_COLORS:ng_display_test(s);break;
    case CONSOLE_PAGE_TOUCH:ng_touch_test(s);break;
    case CONSOLE_PAGE_TERMINAL:ng_terminal(s);break;
    case CONSOLE_PAGE_MULTIPLAYER:ng_multiplayer(s);break;
    case CONSOLE_PAGE_EXTERNAL:ng_launch(s);break;
    }
    if(s->shell->page!=CONSOLE_PAGE_HOME&&s->shell->page!=CONSOLE_PAGE_CONTROL_PANEL&&s->shell->page!=CONSOLE_PAGE_EXTERNAL)
        ng_button(s,1152,82,100,80,"Back",NC_BACK,0,true,false);
    if(s->shell->page==CONSOLE_PAGE_HOME&&s->shell->home_all_programs){
        ng_button(s,264,642,200,56,"Saves",NC_PAGE,CONSOLE_PAGE_SAVES,true,false);
        ng_button(s,488,642,276,56,"Achievements",NC_PAGE,CONSOLE_PAGE_ACHIEVEMENTS,true,false);}
    if(s->shell->page==CONSOLE_PAGE_FILES){
        ng_button(s,580,82,284,80,"Manage games",NC_PAGE,CONSOLE_PAGE_GAMES,true,false);
        ng_button(s,880,82,240,80,"Transfer",NC_PAGE,CONSOLE_PAGE_FILE_TRANSFER,true,false);
    }
    if(s->shell->ng_categories)ng_category_sheet(s);
    if(s->shell->ng_file_menu)ng_file_menu(s);
    if(s->shell->file_delete_confirm||s->shell->storage_repair_confirm)ng_modal(s);
    if(s->shell->pointer_visible){
        const int x=(int)s->shell->pointer_x*NG_W/CONSOLE_SHELL_LAYOUT_WIDTH,y=(int)s->shell->pointer_y*NG_H/CONSOLE_SHELL_LAYOUT_HEIGHT;
        ng_line(s,x,y,x,y+22,NG_WHITE);ng_line(s,x,y,x+16,y+16,NG_WHITE);ng_line(s,x,y+22,x+7,y+16,NG_WHITE);}
}
static const ng_control_t *ng_control(const ng_scene_t *s,uint16_t id)
{ for(size_t i=0;i<s->count;++i)if(s->controls[i].id==id)return &s->controls[i];return NULL; }
static const ng_control_t *ng_at(const ng_scene_t *s,uint16_t x,uint16_t y)
{
    for(size_t i=s->count;i>0U;--i){const ng_control_t *c=&s->controls[i-1U];
        if(c->enabled&&x>=(uint16_t)c->x&&y>=(uint16_t)c->y&&x<(uint16_t)(c->x+c->w)&&y<(uint16_t)(c->y+c->h))return c;}
    return NULL;
}
static void ng_initial_focus(console_shell_t *sh)
{
    /* Keep the displayed content focus attached to its row throughout motion,
     * even after it moves offscreen; physical translation preserves its ring. */
    if(sh->ng_scroll_kind||sh->ng_glide_kind)return;
    ng_scene_t scene={.shell=sh};ng_build_scene(&scene);
    const ng_control_t *current=ng_control(&scene,sh->ng_focus);
    if(current&&current->enabled)return;
    sh->ng_focus=0U;
    for(size_t i=0;i<scene.count;++i)if(scene.controls[i].enabled&&scene.controls[i].x>=264&&scene.controls[i].y>=150){
        sh->ng_focus=scene.controls[i].id;return;}
    if(scene.count)sh->ng_focus=scene.controls[0].id;
}
static console_shell_action_t ng_open(console_shell_t *sh,console_page_t page)
{
    /* Re-tapping the active rail must not silently abandon a joined room. */
    if(page==CONSOLE_PAGE_MULTIPLAYER&&sh->page==page)return no_action();
    sh->control_panel_active=false;sh->file_delete_confirm=false;sh->storage_repair_confirm=false;
    sh->ng_list_first=0U;sh->ng_file_scroll=0;sh->ng_focus=0U;sh->ng_keyboard=false;
    sh->ng_categories=false;sh->ng_file_menu=false;sh->ng_scroll_kind=0;sh->ng_content_drag_started=false;sh->ng_deferred_content_focus=false;
    sh->press_active=false;sh->pressed_index=SIZE_MAX;
    console_shell_action_t action=no_action();
    if(page==CONSOLE_PAGE_HOME){console_shell_show_home(sh);sh->home_all_programs=false;action=page_changed(0U);}
    else {
        bool found=false;
        for(size_t i=0;i<sh->app_count;++i)if(sh->apps[i].page==page&&sh->apps[i].enabled){
            action=activate_app_index(sh,i);found=true;break;}
        if(!found){sh->page=page;sh->active_app_id=0U;sh->dirty=true;action=page_changed(0U);}
    }
    ng_initial_focus(sh);return action;
}
static console_shell_action_t ng_back(console_shell_t *sh)
{
    sh->ng_glide_kind=0;sh->ng_velocity_q16=0;
    if(sh->ng_categories||sh->ng_file_menu){sh->ng_categories=false;sh->ng_file_menu=false;sh->ng_focus=0U;sh->dirty=true;ng_initial_focus(sh);return no_action();}
    if(sh->file_delete_confirm||sh->storage_repair_confirm){
        sh->file_delete_confirm=false;sh->storage_repair_confirm=false;sh->ng_focus=0U;sh->dirty=true;
        ng_initial_focus(sh);return no_action();}
    if(sh->ng_keyboard){sh->ng_keyboard=false;sh->ng_focus=0U;sh->dirty=true;ng_initial_focus(sh);return no_action();}
    if(sh->page==CONSOLE_PAGE_MULTIPLAYER&&sh->multiplayer_view!=CONSOLE_MULTIPLAYER_VIEW_GAME){
        sh->ng_list_first=0U;sh->ng_focus=0U;return multiplayer_back_action(sh);}
    if(sh->page==CONSOLE_PAGE_HOME){
        if(sh->home_all_programs)return ng_open(sh,CONSOLE_PAGE_HOME);
        return no_action();}
    if(sh->page==CONSOLE_PAGE_CONTROL_PANEL)return ng_open(sh,CONSOLE_PAGE_HOME);
    if(sh->page==CONSOLE_PAGE_FILES&&sh->files.can_go_up)
        return (console_shell_action_t){.type=CONSOLE_ACTION_FILE_UP,.app_id=sh->active_app_id,.file_source_index=UINT32_MAX};
    if(ng_nav_selection(sh)==4U)return ng_open(sh,CONSOLE_PAGE_CONTROL_PANEL);
    console_shell_action_t action=ng_open(sh,CONSOLE_PAGE_HOME);sh->home_all_programs=true;sh->ng_focus=0U;ng_initial_focus(sh);return action;
}
static console_shell_action_t ng_activate(console_shell_t *sh,uint16_t id)
{
    sh->ng_glide_kind=0;sh->ng_velocity_q16=0;
    const unsigned kind=id/128U,param=id%128U;
    ng_scene_t scene={.shell=sh};ng_build_scene(&scene);
    const ng_control_t *control=ng_control(&scene,id);
    if(!control||!control->enabled)return no_action();
    sh->dirty=true;
    switch(kind){
    case NC_HOME:return ng_open(sh,CONSOLE_PAGE_HOME);
    case NC_LIBRARY:{console_shell_action_t a=ng_open(sh,CONSOLE_PAGE_HOME);sh->home_all_programs=true;sh->ng_focus=0U;ng_initial_focus(sh);return a;}
    case NC_MULTIPLAYER:return ng_open(sh,CONSOLE_PAGE_MULTIPLAYER);
    case NC_FILES:return ng_open(sh,CONSOLE_PAGE_FILES);
    case NC_SETTINGS:return ng_open(sh,CONSOLE_PAGE_CONTROL_PANEL);
    case NC_BACK:return ng_back(sh);
    case NC_PAGE:return ng_open(sh,(console_page_t)param);
    case NC_LAUNCH:return activate_app_index(sh,param);
    case NC_PREV:ng_set_scroll(sh,true,sh->ng_library_scroll-420);sh->ng_focus=0U;break;
    case NC_NEXT:ng_set_scroll(sh,true,sh->ng_library_scroll+420);sh->ng_focus=0U;break;
    case NC_FILTER:sh->ng_categories=true;sh->ng_category_first=0;sh->ng_focus=ng_id(NC_CANCEL,0);break;
    case NC_CATEGORY:{const char *paths[CONSOLE_SHELL_MAX_APPS+1];size_t n=ng_categories(sh,paths);
        if(param<n){(void)snprintf(sh->ng_category,sizeof(sh->ng_category),"%s",paths[param]);sh->ng_library_scroll=0;}
        sh->ng_categories=false;sh->ng_focus=0;break;}
    case NC_CATEGORY_PREV:sh->ng_category_first=sh->ng_category_first>=12U?sh->ng_category_first-12U:0U;sh->ng_focus=ng_id(NC_CANCEL,0);break;
    case NC_CATEGORY_NEXT:sh->ng_category_first+=12U;sh->ng_focus=ng_id(NC_CANCEL,0);break;
    case NC_DISMISS:return ng_back(sh);
    case NC_SCROLL:break;
    case NC_FILE_MENU:sh->ng_file_menu=true;sh->ng_confirm_revision=sh->files.revision;
        sh->ng_confirm_source=sh->files.entries[sh->file_selected_index].source_index;sh->ng_focus=ng_id(NC_CANCEL,0);break;
    case NC_FILE_ROW:sh->file_selected_index=param;sh->file_delete_confirm=false;break;
    case NC_FILE_PREV:ng_set_scroll(sh,false,sh->ng_file_scroll-360);sh->ng_focus=0;break;
    case NC_FILE_NEXT:ng_set_scroll(sh,false,sh->ng_file_scroll+360);sh->ng_focus=0;break;
    case NC_REFRESH:return (console_shell_action_t){.type=sh->page==CONSOLE_PAGE_GAMES?CONSOLE_ACTION_GAME_REFRESH:CONSOLE_ACTION_FILE_REFRESH,.app_id=sh->active_app_id,.file_source_index=UINT32_MAX};
    case NC_FILE_OPEN:
        if(param==1U)return (console_shell_action_t){.type=CONSOLE_ACTION_FILE_UP,.app_id=sh->active_app_id,.file_source_index=UINT32_MAX};
        if(!file_selected_is_openable(sh)&&!file_selected_is_installable(sh))return no_action();
        sh->ng_confirm_revision=sh->files.revision;sh->ng_confirm_source=sh->file_selected_index<sh->files.entry_count?sh->files.entries[sh->file_selected_index].source_index:UINT32_MAX;
        {console_shell_action_t a=file_selected_action(sh);if(sh->file_delete_confirm)sh->ng_focus=ng_id(NC_CANCEL,0);return a;}
    case NC_DELETE:
        if(!sh->ng_file_menu||!file_selected_is_removable(sh))return no_action();
        sh->ng_file_menu=false;sh->file_delete_confirm=true;sh->ng_confirm_revision=sh->files.revision;
        sh->ng_confirm_source=sh->files.entries[sh->file_selected_index].source_index;sh->ng_focus=ng_id(NC_CANCEL,0);break;
    case NC_CANCEL:return ng_back(sh);
    case NC_CONFIRM:
        if(sh->storage_repair_confirm)return storage_action(sh,2U);
        if(!sh->file_delete_confirm||sh->ng_confirm_revision!=sh->files.revision||
            sh->file_selected_index>=sh->files.entry_count||sh->ng_confirm_source!=sh->files.entries[sh->file_selected_index].source_index){
            sh->file_delete_confirm=false;return no_action();}
        return file_selected_action(sh);
    case NC_AUDIO_STATUS:return ng_open(sh,CONSOLE_PAGE_AUDIO);
    case NC_AUDIO:return audio_volume_action(sh,param<2U,param%2U?1:-1);
    case NC_STORAGE:{console_shell_action_t a=storage_action(sh,param);if(sh->storage_repair_confirm)sh->ng_focus=ng_id(NC_CANCEL,0);return a;}
    case NC_CONTROLLER:return controller_action(sh,param);
    case NC_USB:return system_usb_action(sh);
    case NC_MP_VIEW:sh->ng_list_first=0U;sh->ng_focus=0U;return enter_multiplayer_view(sh,(console_multiplayer_view_t)param);
    case NC_MP_OPTIONS:sh->ng_list_first=0U;sh->ng_focus=0U;return enter_multiplayer_host_settings(sh);
    case NC_MP_GAME:
        if(sh->multiplayer_view!=CONSOLE_MULTIPLAYER_VIEW_GAME||
           param>=sh->runtime.multiplayer_game_count||param>=CONSOLE_MULTIPLAYER_MAX_GAMES||
           !sh->runtime.multiplayer_games[param].available)return no_action();
        sh->multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_TRANSPORT;sh->ng_list_first=0U;sh->ng_focus=0U;
        return (console_shell_action_t){.type=CONSOLE_ACTION_MULTIPLAYER_GAME_SELECT,
            .app_id=sh->active_app_id,.file_source_index=UINT32_MAX,.multiplayer_game_selection=(uint8_t)param};
    case NC_MP_TRANSPORT:
        if(sh->multiplayer_view!=CONSOLE_MULTIPLAYER_VIEW_TRANSPORT||param>2U)return no_action();
        sh->multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_ROLE;sh->ng_focus=0U;
        return (console_shell_action_t){.type=CONSOLE_ACTION_MULTIPLAYER_TRANSPORT_SELECT,
            .app_id=sh->active_app_id,.file_source_index=UINT32_MAX,.multiplayer_transport_kind=(uint8_t)param};
    case NC_MP_OPTION:return multiplayer_config_action(sh,(console_multiplayer_option_t)(param/2U),param%2U?1:-1);
    case NC_MP_PRIMARY:return multiplayer_primary_action(sh);
    case NC_MP_ROOM:return multiplayer_lobby_select_action(sh,(uint8_t)param);
    case NC_KEYBOARD:sh->ng_keyboard=!sh->ng_keyboard;sh->ng_focus=0U;break;
    case NC_KEY:(void)console_shell_handle_text_key(sh,(char)param);break;
    case NC_APPEARANCE:if(param==0U)sh->ng_large_text=!sh->ng_large_text;else sh->ng_reduce_motion=!sh->ng_reduce_motion;break;
    case NC_LIST_PREV:{const size_t step=ng_list_page_size(sh);sh->ng_list_first=sh->ng_list_first>=step?sh->ng_list_first-step:0U;break;}
    case NC_LIST_NEXT:sh->ng_list_first+=ng_list_page_size(sh);break;
    default:break;
    }
    ng_initial_focus(sh);return no_action();
}
static console_shell_action_t nextgen_buttons(console_shell_t *sh,uint32_t pressed)
{
    if(pressed){sh->ng_glide_kind=0;sh->ng_velocity_q16=0;}
    if(pressed&&sh->contact_down&&sh->ng_content_drag_started&&sh->ng_scroll_kind>0U&&sh->ng_scroll_kind<3U){
        sh->ng_focus=0;sh->ng_deferred_content_focus=false;
    }
    if(pressed&CONSOLE_BUTTON_BACK)return ng_back(sh);
    ng_initial_focus(sh);ng_scene_t scene={.shell=sh};ng_build_scene(&scene);
    const ng_control_t *from=ng_control(&scene,sh->ng_focus);
    if(pressed&CONSOLE_BUTTON_ACCEPT)return ng_activate(sh,sh->ng_focus);
    if((pressed&CONSOLE_BUTTON_REFRESH)&&(sh->page==CONSOLE_PAGE_FILES||sh->page==CONSOLE_PAGE_GAMES))return ng_activate(sh,ng_id(NC_REFRESH,0));
    if(!from)return no_action();
    const bool library=sh->page==CONSOLE_PAGE_HOME&&sh->home_all_programs;
    const unsigned kind=from->id/128U;
    if((library&&kind==NC_LAUNCH)||((sh->page==CONSOLE_PAGE_FILES||sh->page==CONSOLE_PAGE_GAMES)&&kind==NC_FILE_ROW)){
        size_t indices[CONSOLE_SHELL_MAX_APPS];const size_t count=library?ng_library_games(sh,indices):sh->files.entry_count;
        size_t index=from->id%128U;
        if(library){index=0;while(index<count&&indices[index]!=from->id%128U)++index;}
        int delta=0;
        if(pressed&CONSOLE_BUTTON_DOWN)delta=library?3:1;
        else if(pressed&CONSOLE_BUTTON_UP)delta=library?-3:-1;
        else if(library&&(pressed&CONSOLE_BUTTON_RIGHT))delta=1;
        else if(library&&(pressed&CONSOLE_BUTTON_LEFT)&&index%3U!=0U)delta=-1;
        const int next=(int)index+delta;
        if(delta&&next>=0&&(size_t)next<count){ng_reveal(sh,(size_t)next,library);
            sh->ng_focus=ng_id(library?NC_LAUNCH:NC_FILE_ROW,(unsigned)(library?indices[next]:(size_t)next));
            if(!library)sh->file_selected_index=(size_t)next;
            sh->dirty=true;sh->ng_focus_ms=100U;return no_action();}
    }
    const int cx=from->x+from->w/2,cy=from->y+from->h/2;
    int best=INT_MAX;uint16_t target=sh->ng_focus;
    for(size_t i=0;i<scene.count;++i){
        const ng_control_t *c=&scene.controls[i];if(!c->enabled||c->id==from->id)continue;
        const int dx=c->x+c->w/2-cx,dy=c->y+c->h/2-cy;int forward=0,side=0;
        if(pressed&CONSOLE_BUTTON_RIGHT){forward=dx;side=dy;}
        else if(pressed&CONSOLE_BUTTON_LEFT){forward=-dx;side=dy;}
        else if(pressed&CONSOLE_BUTTON_DOWN){forward=dy;side=dx;}
        else if(pressed&CONSOLE_BUTTON_UP){forward=-dy;side=dx;}
        if(forward<=12)continue;
        if(side<0)side=-side;
        const int score=forward+side*4;if(score<best){best=score;target=c->id;}
    }
    if(target!=sh->ng_focus){sh->ng_focus=target;sh->dirty=true;sh->ng_focus_ms=100U;}
    return no_action();
}
static bool ng_content_press(const console_shell_t *sh,size_t id)
{
    return (sh->page==CONSOLE_PAGE_HOME&&sh->home_all_programs&&id/128U==NC_LAUNCH)||
        ((sh->page==CONSOLE_PAGE_FILES||sh->page==CONSOLE_PAGE_GAMES)&&id/128U==NC_FILE_ROW);
}
static bool ng_commit_content_focus(console_shell_t *sh)
{
    if(!sh->ng_deferred_content_focus||!sh->contact_down||!sh->press_active||sh->ng_content_drag_started||
       !ng_content_press(sh,sh->pressed_index)||sh->pressed_index>UINT16_MAX||sh->pointer_visible||
       sh->ng_categories||sh->ng_file_menu||sh->file_delete_confirm||sh->storage_repair_confirm)return false;
    const size_t id=sh->pressed_index;
    if(id/128U==NC_LAUNCH&&(id%128U>=sh->app_count||sh->apps[id%128U].id!=sh->ng_press_identity))return false;
    if(id/128U==NC_FILE_ROW&&sh->ng_press_revision!=sh->files.revision)return false;
    sh->ng_focus=(uint16_t)id;sh->ng_focus_ms=100;sh->ng_deferred_content_focus=false;sh->dirty=true;
    return true;
}
static bool ng_content_drag_eligible(const console_shell_t *sh)
{
    if(!sh->contact_down||!sh->ng_content_drag_started||sh->press_active||sh->ng_reduce_motion||sh->pointer_visible||
       sh->ng_categories||sh->ng_file_menu||sh->file_delete_confirm||sh->storage_repair_confirm)return false;
    if(sh->ng_scroll_kind==1U)return sh->page==CONSOLE_PAGE_HOME&&sh->home_all_programs;
    return sh->ng_scroll_kind==2U&&(sh->page==CONSOLE_PAGE_FILES||sh->page==CONSOLE_PAGE_GAMES)&&
        sh->ng_press_revision==sh->files.revision;
}
static console_shell_action_t nextgen_touch(console_shell_t *sh,bool valid,const console_shell_contact_t *contacts,size_t count)
{
    remember_contacts(sh,valid,contacts,count);
    if(!valid||count>1U||(count&&!contacts)){
        sh->contact_down=true;sh->press_active=false;sh->pressed_index=SIZE_MAX;sh->ng_scroll_kind=0;sh->ng_content_drag_started=false;sh->ng_deferred_content_focus=false;sh->ng_glide_kind=0;sh->ng_velocity_q16=0;sh->dirty=true;return no_action();}
    ng_scene_t scene={.shell=sh};ng_build_scene(&scene);
    if(count==0U){
        if(!sh->contact_down)return no_action();
        if(ng_content_drag_eligible(sh)&&
           sh->animation_clock_ms-sh->ng_sample_ms<=80U&&abs(sh->ng_velocity_q16)>=6554){
            sh->ng_glide_kind=sh->ng_scroll_kind;
            sh->ng_glide_q16=(sh->ng_scroll_kind==1U?sh->ng_library_scroll:sh->ng_file_scroll)*65536;
        }
        const size_t id=sh->pressed_index;bool activate=sh->press_active&&id<=UINT16_MAX;
        if(activate&&id/128U==NC_LAUNCH&&(id%128U>=sh->app_count||sh->apps[id%128U].id!=sh->ng_press_identity))activate=false;
        if(activate&&id/128U==NC_FILE_ROW&&sh->ng_press_revision!=sh->files.revision)activate=false;
        if(activate&&id/128U==NC_MP_ROOM){
            const size_t row=id%128U;
            if(row==0U||row>sh->runtime.multiplayer_lobby_count||row>CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX||
                sh->runtime.multiplayer_lobbies[row-1U].session_id!=sh->ng_press_identity)activate=false;
        }
        if(activate&&sh->ng_deferred_content_focus)(void)ng_commit_content_focus(sh);
        sh->contact_down=false;sh->press_active=false;sh->pressed_index=SIZE_MAX;sh->ng_scroll_kind=0;
        sh->ng_content_drag_started=false;sh->ng_deferred_content_focus=false;sh->dirty=true;
        if(activate)return ng_activate(sh,(uint16_t)id);
        return no_action();
    }
    const uint16_t x=contacts[0].x,y=contacts[0].y;
    if(x>=NG_W||y>=NG_H){sh->contact_down=true;sh->press_active=false;sh->ng_scroll_kind=0;sh->ng_content_drag_started=false;sh->ng_deferred_content_focus=false;sh->ng_glide_kind=0;sh->ng_velocity_q16=0;sh->dirty=true;return no_action();}
    const ng_control_t *hit=ng_at(&scene,x,y);
    if(!sh->contact_down){
        const bool stopping=sh->ng_glide_kind!=0U;
        sh->ng_glide_kind=0;sh->ng_velocity_q16=0;sh->ng_sample_ms=sh->animation_clock_ms;
        sh->contact_down=true;sh->press_start_physical_x=x;sh->press_start_physical_y=y;sh->ng_scroll_kind=0;sh->ng_content_drag_started=false;sh->ng_deferred_content_focus=false;
        sh->press_active=hit!=NULL;sh->pressed_index=hit?hit->id:SIZE_MAX;sh->ng_press_revision=sh->files.revision;
        const bool modal=sh->file_delete_confirm||sh->storage_repair_confirm||sh->ng_categories||sh->ng_file_menu;
        const bool library=sh->page==CONSOLE_PAGE_HOME&&sh->home_all_programs;
        const bool files=sh->page==CONSOLE_PAGE_FILES||sh->page==CONSOLE_PAGE_GAMES;
        if(!modal&&x>=264U&&((library&&y>=NG_GRID_TOP&&y<NG_GRID_BOTTOM)||(files&&y>=NG_FILES_TOP&&y<NG_FILES_BOTTOM))){
            sh->ng_scroll_kind=library?1U:2U;sh->ng_scroll_origin=library?sh->ng_library_scroll:sh->ng_file_scroll;sh->ng_sample_scroll=sh->ng_scroll_origin;
            if(hit&&hit->id/128U==NC_SCROLL){sh->ng_scroll_kind+=2U;sh->press_active=false;}}
        if(stopping)sh->press_active=false; /* A touch that stops a fling must not launch. */
        if(hit){
            const bool content=ng_content_press(sh,hit->id);
            sh->ng_deferred_content_focus=content&&!stopping;
            if(!content&&!stopping){sh->ng_focus=hit->id;sh->ng_focus_ms=100U;}
            sh->ng_press_identity=hit->id/128U==NC_LAUNCH&&hit->id%128U<sh->app_count?sh->apps[hit->id%128U].id:0U;
            if(hit->id/128U==NC_MP_ROOM){
                const size_t row=hit->id%128U;
                if(row>0U&&row<=sh->runtime.multiplayer_lobby_count&&row<=CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX)
                    sh->ng_press_identity=sh->runtime.multiplayer_lobbies[row-1U].session_id;
            }
        }
        sh->dirty=true;
    }else {
        const int dx=(int)x-(int)sh->press_start_physical_x,dy=(int)y-(int)sh->press_start_physical_y;
        const bool drag=sh->ng_scroll_kind>0U&&sh->ng_scroll_kind<3U&&(dy>=6||dy<= -6);
        if(drag){sh->ng_content_drag_started=true;sh->ng_deferred_content_focus=false;}
        if(sh->press_active&&(drag||dx>14||dx< -14||dy>14||dy< -14||!hit||hit->id!=sh->pressed_index)){
            sh->press_active=false;sh->ng_deferred_content_focus=false;sh->dirty=true;}
        if(sh->ng_scroll_kind&&(!sh->press_active||sh->ng_scroll_kind>=3U)){
            const bool library=sh->ng_scroll_kind==1U||sh->ng_scroll_kind==3U;
            if(sh->ng_scroll_kind>=3U){
                size_t indices[CONSOLE_SHELL_MAX_APPS];const size_t n=library?ng_library_games(sh,indices):sh->files.entry_count;
                const int top=library?NG_GRID_TOP:NG_FILES_TOP,view=library?NG_GRID_BOTTOM-NG_GRID_TOP:NG_FILES_BOTTOM-NG_FILES_TOP;
                const int limit=ng_scroll_max(n,library);int thumb=view*view/(view+limit);if(thumb<48)thumb=48;
                if(view>thumb)ng_set_scroll(sh,library,((int)y-top-thumb/2)*limit/(view-thumb));
            }else {
                ng_set_scroll(sh,library,sh->ng_scroll_origin-dy);
                const int offset=library?sh->ng_library_scroll:sh->ng_file_scroll;
                const uint32_t dt=sh->animation_clock_ms-sh->ng_sample_ms;
                if(dt){
                    int32_t velocity=dt<=80U?(int32_t)((int64_t)(offset-sh->ng_sample_scroll)*65536/(int64_t)dt):0;
                    if(velocity>196608)velocity=196608;
                    if(velocity< -196608)velocity= -196608;
                    sh->ng_velocity_q16=(sh->ng_velocity_q16+velocity)/2;
                    sh->ng_sample_ms=sh->animation_clock_ms;sh->ng_sample_scroll=offset;
                }
            }
            /* Retain content focus so the already displayed highlight moves
             * with its card/row and needs no full-viewport repair on onset. */
        }
    }
    return no_action();
}
static bool nextgen_advance(console_shell_t *sh,uint32_t elapsed)
{
    sh->animation_clock_ms+=elapsed;
    bool changed=false;
    if(sh->ng_focus_ms&&!sh->ng_reduce_motion){
        sh->ng_focus_ms=elapsed>=sh->ng_focus_ms?0U:(uint16_t)(sh->ng_focus_ms-elapsed);changed=true;
    }
    if(sh->ng_glide_kind){
        const bool library=sh->ng_glide_kind==1U;
        const bool eligible=library?(sh->page==CONSOLE_PAGE_HOME&&sh->home_all_programs):
            (sh->page==CONSOLE_PAGE_FILES||sh->page==CONSOLE_PAGE_GAMES);
        if(!eligible||sh->contact_down||sh->ng_reduce_motion||sh->pointer_visible||sh->ng_categories||sh->ng_file_menu||
           sh->file_delete_confirm||sh->storage_repair_confirm||(!library&&sh->ng_press_revision!=sh->files.revision)||elapsed>100U){
            sh->ng_glide_kind=0;sh->ng_velocity_q16=0;
        }else{
            /* Integrate at 1 ms so every cadence follows the same Q16 path.
             * Proportional damping eases a fast flick; the small deceleration
             * floor brings slow drags to rest without a long fractional tail. */
            const int before=library?sh->ng_library_scroll:sh->ng_file_scroll;
            size_t games[CONSOLE_SHELL_MAX_APPS];
            const size_t count=library?ng_library_games(sh,games):sh->files.entry_count;
            const int32_t maximum=ng_scroll_max(count,library)*65536;
            for(uint32_t ms=0;ms<elapsed&&sh->ng_velocity_q16;++ms){
                const int32_t old=sh->ng_velocity_q16;
                const int64_t speed=old<0?-(int64_t)old:old;
                int64_t decrement=(speed+179)/180;
                if(decrement<96){decrement=96;}
                const int32_t next=(int32_t)(speed>decrement?speed-decrement:0);
                sh->ng_velocity_q16=old<0?-next:next;
                sh->ng_glide_q16+=(old+sh->ng_velocity_q16)/2;
                if(sh->ng_glide_q16<0){sh->ng_glide_q16=0;sh->ng_velocity_q16=0;}
                else if(sh->ng_glide_q16>maximum){sh->ng_glide_q16=maximum;sh->ng_velocity_q16=0;}
            }
            const int desired=sh->ng_glide_q16/65536;ng_set_scroll(sh,library,desired);
            const int after=library?sh->ng_library_scroll:sh->ng_file_scroll;
            if(after!=desired||!sh->ng_velocity_q16){sh->ng_glide_kind=0;sh->ng_velocity_q16=0;}
            changed|=before!=after;
        }
    }
    if(changed)sh->dirty=true;
    return changed;
}
enum { NG_RASTER_STRIDE=1024,NG_LIBRARY_RASTER_HEIGHT=1280,NG_FILE_RASTER_HEIGHT=768,
    NG_RASTER_BYTES=4*1024*1024 };

static uint64_t ng_catalog_signature(const console_shell_t *sh)
{
    uint64_t hash=UINT64_C(1469598103934665603);
    hash=home_cache_hash_bytes(hash,&sh->app_count,sizeof(sh->app_count));
    for(size_t i=0;i<sh->app_count;++i){
        const console_app_descriptor_t *app=&sh->apps[i];
        hash=home_cache_hash_bytes(hash,app,sizeof(*app));
        hash=home_cache_hash_text(hash,app->title,CONSOLE_SHELL_TITLE_MAX_BYTES);
        hash=home_cache_hash_text(hash,app->subtitle,CONSOLE_SHELL_SUBTITLE_MAX_BYTES);
        hash=home_cache_hash_text(hash,app->folder_path,CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES);
        if(app->cover)hash=home_cache_hash_bytes(hash,app->cover,sizeof(*app->cover));
    }
    return hash;
}
static uint64_t ng_raster_signature(const console_shell_t *sh,bool library)
{
    uint64_t hash=home_cache_hash_bytes(UINT64_C(1469598103934665603),&sh->ng_large_text,sizeof(sh->ng_large_text));
    if(library){
        const uint64_t catalog=ng_catalog_signature(sh);
        hash=home_cache_hash_bytes(hash,&catalog,sizeof(catalog));
        hash=home_cache_hash_bytes(hash,sh->ng_category,sizeof(sh->ng_category));
    }else hash=home_cache_hash_bytes(hash,&sh->files,sizeof(sh->files));
    return hash;
}
static size_t ng_raster_count(const console_shell_t *sh,bool library)
{
    if(!library)return sh->files.available?sh->files.entry_count:0U;
    size_t games[CONSOLE_SHELL_MAX_APPS],count=ng_games(sh,games),filtered=0;
    for(size_t i=0;i<count;++i)
        if(!sh->ng_category[0]||strcmp(ng_app_category(&sh->apps[games[i]]),sh->ng_category)==0)++filtered;
    return filtered;
}
static bool ng_raster_covers(const console_shell_t *sh,bool library)
{
    const console_shell_native_raster_cache_t *cache=&sh->native_rasters[library?0:1];
    const int offset=library?sh->ng_library_scroll:sh->ng_file_scroll;
    const int view=library?NG_GRID_BOTTOM-NG_GRID_TOP:NG_FILES_BOTTOM-NG_FILES_TOP;
    return cache->pixels&&cache->valid&&cache->signature==ng_raster_signature(sh,library)&&
        offset>=cache->content_origin&&offset+view<=cache->content_origin+(int)cache->height;
}
static bool ng_raster_dma_clean(const console_shell_native_raster_cache_t *cache)
{
    return cache->valid&&cache->preparation_epoch!=0&&
        cache->published_epoch==cache->preparation_epoch&&cache->published_rows==cache->height;
}
static bool ng_prepare_raster(console_shell_t *sh,bool library,uint16_t rows)
{
    console_shell_native_raster_cache_t *cache=&sh->native_rasters[library?0:1];
    const size_t count=ng_raster_count(sh,library);if(!cache->pixels||!count)return false;
    if(ng_raster_covers(sh,library)&&(!sh->native_publish||ng_raster_dma_clean(cache)))return false;
    const uint64_t signature=ng_raster_signature(sh,library);
    const int view=library?NG_GRID_BOTTOM-NG_GRID_TOP:NG_FILES_BOTTOM-NG_FILES_TOP;
    const int top=library?NG_GRID_TOP:NG_FILES_TOP,limit=ng_scroll_max(count,library);
    int offset=library?sh->ng_library_scroll:sh->ng_file_scroll;
    if(offset<0){offset=0;}
    if(offset>limit){offset=limit;}
    if(cache->preparation_epoch==0||cache->signature!=signature||offset<cache->content_origin||offset+view>cache->content_origin+(int)cache->height){
        int origin=offset-((int)cache->height-view)/2,max_origin=limit+view-(int)cache->height;
        if(max_origin<0){max_origin=0;}
        if(origin<0){origin=0;}
        if(origin>max_origin){origin=max_origin;}
        /* A recenter can repaint the same address with the same content hash.
         * Revoke the previous publication before any source pixel is written. */
        if(cache->preparation_epoch==UINT64_MAX)return false;
        ++cache->preparation_epoch;
        cache->published_epoch=0;cache->published_rows=0;
        cache->signature=signature;cache->content_origin=origin;cache->prepared_rows=0;cache->valid=false;
    }
    const uint16_t remaining=(uint16_t)(cache->height-cache->prepared_rows);
    const uint16_t painted=rows>remaining?remaining:rows;
    if(painted){
        ng_scene_t scene={.shell=sh,.pixels=cache->pixels,.stride=NG_RASTER_STRIDE,
            .origin_x=264,.origin_y=top,.width=964,.height=cache->height,
            .clip_top=top+(int)cache->prepared_rows,.clip_bottom=top+(int)cache->prepared_rows+(int)painted,
            .raster_scroll=cache->content_origin,.cache_base=true};
        ng_rect(&scene,264,scene.clip_top,964,(int)painted,NG_INK);
        if(library){size_t games[CONSOLE_SHELL_MAX_APPS];const size_t n=ng_library_games(sh,games);ng_library_rows(&scene,games,n);}
        else ng_file_rows(&scene,count);
        cache->prepared_rows=(uint16_t)(cache->prepared_rows+painted);cache->valid=cache->prepared_rows==cache->height;
    }
    bool published=false;
    if(sh->native_publish&&cache->published_rows<cache->prepared_rows){
        const uint16_t dirty=(uint16_t)(cache->prepared_rows-cache->published_rows);
        const console_shell_rgb565_publication_t publication={.pixels=cache->pixels,
            .stride_pixels=NG_RASTER_STRIDE,.height=cache->height,.first_row=cache->published_rows,
            .row_count=dirty>rows?rows:dirty};
        published=true;
        if(sh->native_publish(sh->native_copy_context,&publication)){
            cache->published_epoch=cache->preparation_epoch;
            cache->published_rows=(uint16_t)(cache->published_rows+publication.row_count);
        }
    }
    return painted>0||published;
}
static uint64_t ng_chrome_signature(const console_shell_t *sh)
{
    uint64_t hash=UINT64_C(1469598103934665603);
#define NG_HASH(field) hash=home_cache_hash_bytes(hash,&sh->field,sizeof(sh->field))
    NG_HASH(runtime.game_volume_step);NG_HASH(runtime.game_storage_space_valid);
    NG_HASH(runtime.game_storage_free_kib);NG_HASH(runtime.game_storage_state);
    NG_HASH(runtime.battery_sample_valid);NG_HASH(runtime.battery_percent);
#undef NG_HASH
    return hash;
}
static uint64_t ng_structure_signature(const console_shell_t *sh)
{
    uint64_t hash=UINT64_C(1469598103934665603);
#define NG_HASH(field) hash=home_cache_hash_bytes(hash,&sh->field,sizeof(sh->field))
    NG_HASH(page);NG_HASH(home_all_programs);NG_HASH(ng_large_text);NG_HASH(ng_reduce_motion);
    NG_HASH(ng_category);NG_HASH(ng_categories);NG_HASH(ng_file_menu);NG_HASH(ng_keyboard);
    NG_HASH(file_delete_confirm);NG_HASH(storage_repair_confirm);NG_HASH(pointer_visible);
#undef NG_HASH
    return hash;
}
static uint64_t ng_content_signature(const console_shell_t *sh)
{
    uint64_t hash=ng_catalog_signature(sh);
#define NG_HASH(field) hash=home_cache_hash_bytes(hash,&sh->field,sizeof(sh->field))
    if(sh->page==CONSOLE_PAGE_FILES||sh->page==CONSOLE_PAGE_GAMES){NG_HASH(files);NG_HASH(file_notice);}
    else if(sh->page==CONSOLE_PAGE_EXTERNAL){
        /* Launch progress is the only dynamic region; title/app identity must
         * invalidate its static artwork and chrome before that region reuses. */
        NG_HASH(active_app_id);NG_HASH(loading.active);NG_HASH(loading.title);
    }else if(sh->page!=CONSOLE_PAGE_HOME&&sh->page!=CONSOLE_PAGE_CONTROL_PANEL){
        NG_HASH(runtime);NG_HASH(active_app_id);NG_HASH(saves);NG_HASH(achievements);NG_HASH(terminal);
        NG_HASH(multiplayer_view);NG_HASH(multiplayer_selected_row);NG_HASH(multiplayer_role_selection);
        NG_HASH(ng_list_first);NG_HASH(contacts);NG_HASH(contact_count);NG_HASH(pointer_x);NG_HASH(pointer_y);
    }
#undef NG_HASH
    return hash;
}
static void ng_damage(ng_scene_t *s,int x,int y,int w,int h)
{
    int right=x+w,bottom=y+h;
    if(x<0){x=0;}
    if(y<0){y=0;}
    if(right>NG_W){right=NG_W;}
    if(bottom>NG_H){bottom=NG_H;}
    if(x>=right||y>=bottom)return;
    if(s->damage_right<=s->damage_left){s->damage_left=x;s->damage_top=y;s->damage_right=right;s->damage_bottom=bottom;}
    else{
        if(x<s->damage_left){s->damage_left=x;}
        if(y<s->damage_top){s->damage_top=y;}
        if(right>s->damage_right){s->damage_right=right;}
        if(bottom>s->damage_bottom){s->damage_bottom=bottom;}
    }
}
static void ng_control_damage(ng_scene_t *draw,const ng_scene_t *controls,size_t id)
{
    if(id>UINT16_MAX)return;
    const ng_control_t *control=ng_control(controls,(uint16_t)id);
    if(control)ng_damage(draw,control->x,control->y,control->w,control->h);
}
static void ng_copy_raster_region(console_shell_t *sh,ng_scene_t *scene,bool library,int left,int first_y,int right,int last_y)
{
    const console_shell_native_raster_cache_t *cache=&sh->native_rasters[library?0:1];
    const int top=library?NG_GRID_TOP:NG_FILES_TOP;
    const int offset=library?sh->ng_library_scroll:sh->ng_file_scroll;
    const console_shell_rgb565_copy_t copy={.source=cache->pixels,.source_stride_pixels=NG_RASTER_STRIDE,
        .source_height=cache->height,.destination=scene->pixels,.destination_stride_pixels=scene->stride,.destination_height=NG_H,
        .source_x=(uint16_t)(left-264),.source_y=(uint16_t)(offset-cache->content_origin+first_y-top),.destination_x=(uint16_t)left,.destination_y=(uint16_t)first_y,
        .width=(uint16_t)(right-left),.height=(uint16_t)(last_y-first_y),.source_dma_clean=sh->native_publish&&ng_raster_dma_clean(cache)};
    if(!sh->native_copy||!sh->native_copy(sh->native_copy_context,&copy))
        for(size_t row=0;row<copy.height;++row)
            memcpy(copy.destination+(row+copy.destination_y)*copy.destination_stride_pixels+copy.destination_x,
                copy.source+(row+copy.source_y)*copy.source_stride_pixels+copy.source_x,(size_t)copy.width*sizeof(uint16_t));
    scene->clip_top=first_y;scene->clip_bottom=last_y;
    if(library){
        size_t games[CONSOLE_SHELL_MAX_APPS];const size_t count=ng_library_games(sh,games);
        for(size_t i=0;i<count;++i)if(sh->ng_focus==ng_id(NC_LAUNCH,(unsigned)games[i])){
            const int x=264+(int)(i%3U)*326,y=top+(int)(i/3U)*NG_GRID_PITCH-offset;
            ng_panel_border(scene,x,y,310,194,NG_CYAN,true);
            /* Generic line art uses three-pixel strokes that spill beyond its
             * rectangle into the panel ring. Restore that cheap art in the
             * authoritative draw order after changing the cached border. */
            if(ng_game_art_is_generic(&sh->apps[games[i]]))
                ng_game_art(scene,&sh->apps[games[i]],x+4,y+4,302,129);
        }
    }else for(size_t i=0;i<sh->files.entry_count;++i)
        if(sh->file_selected_index==i||sh->ng_focus==ng_id(NC_FILE_ROW,(unsigned)i))
            ng_panel_border(scene,264,top+(int)i*NG_FILE_PITCH-offset,964,82,NG_CYAN,false);
    scene->clip_top=0;scene->clip_bottom=0;++sh->native_raster_copy_frames;
}
static void ng_copy_raster_strip(console_shell_t *sh,ng_scene_t *scene,bool library,int first_y,int last_y)
{
    ng_copy_raster_region(sh,scene,library,264,first_y,1228,last_y);
}
static void ng_copy_raster(console_shell_t *sh,ng_scene_t *scene,bool library)
{
    ng_copy_raster_strip(sh,scene,library,library?NG_GRID_TOP:NG_FILES_TOP,
        library?NG_GRID_BOTTOM:NG_FILES_BOTTOM);
}
static void ng_stationary_damage(const console_shell_t *sh,ng_scene_t *scene,const ng_scene_t *controls,
    uint64_t chrome,uint16_t focus_ms,bool visible_press,size_t visible_pressed,bool list,int top)
{
    if(chrome!=sh->ng_frame_chrome_signature)ng_damage(scene,0,0,NG_W,80);
    if(sh->ng_focus!=sh->ng_previous_focus||focus_ms!=sh->ng_previous_focus_ms){
        ng_control_damage(scene,controls,sh->ng_previous_focus);ng_control_damage(scene,controls,sh->ng_focus);}
    if(visible_press!=sh->ng_previous_press_active||visible_pressed!=sh->ng_previous_pressed_index){
        ng_control_damage(scene,controls,sh->ng_previous_pressed_index);ng_control_damage(scene,controls,visible_pressed);}
    if(list&&sh->file_selected_index!=sh->ng_previous_file_selected)ng_damage(scene,264,top,988,700-top);
}
static bool nextgen_render(console_shell_t *sh,uint16_t *pixels,size_t stride,bool cached,bool physical_scroll)
{
    if(!sh||!pixels||stride<NG_W||stride>SIZE_MAX/NG_H/sizeof(*pixels))return false;
    if(sh->page!=CONSOLE_PAGE_EXTERNAL)ng_initial_focus(sh);
    const bool library=sh->page==CONSOLE_PAGE_HOME&&sh->home_all_programs;
    bool loading=sh->page==CONSOLE_PAGE_EXTERNAL&&sh->loading.active;
    if(loading){
        bool found=false;
        for(size_t i=0;i<sh->app_count;++i)if(sh->apps[i].id==sh->active_app_id){found=true;break;}
        loading=found;
    }
    const bool list=library||sh->page==CONSOLE_PAGE_FILES||sh->page==CONSOLE_PAGE_GAMES;
    const bool eligible=!sh->ng_categories&&!sh->ng_file_menu&&!sh->file_delete_confirm&&!sh->storage_repair_confirm&&!sh->pointer_visible;
    const uint64_t structure=ng_structure_signature(sh),content=ng_content_signature(sh),chrome=ng_chrome_signature(sh);
    ng_scene_t controls={.shell=sh};ng_build_scene(&controls);
    const ng_control_t *focused=ng_control(&controls,sh->ng_focus);
    const uint16_t focus_ms=focused&&focused->animated?sh->ng_focus_ms:0;
    uint64_t signature=home_cache_hash_bytes(structure,&content,sizeof(content));
    signature=home_cache_hash_bytes(signature,&chrome,sizeof(chrome));
#define NG_HASH(field) signature=home_cache_hash_bytes(signature,&sh->field,sizeof(sh->field))
    NG_HASH(ng_focus);
    /* List cards/rows have only a focus border, never a pressed fill. Their
     * transient hit state must not change the stationary pixel context. */
    const bool visible_press=sh->press_active&&!ng_content_press(sh,sh->pressed_index);
    signature=home_cache_hash_bytes(signature,&visible_press,sizeof(visible_press));
    const size_t visible_pressed=visible_press?sh->pressed_index:0U;
    signature=home_cache_hash_bytes(signature,&visible_pressed,sizeof(visible_pressed));
    NG_HASH(file_selected_index);
#undef NG_HASH
    signature=home_cache_hash_bytes(signature,&focus_ms,sizeof(focus_ms));
    const int offset=library?sh->ng_library_scroll:sh->ng_file_scroll,previous=sh->native_home_cache_scroll_q16;
    const int shift=previous-offset,magnitude=abs(shift),top=library?NG_GRID_TOP:NG_FILES_TOP,bottom=library?NG_GRID_BOTTOM:NG_FILES_BOTTOM;
    const int limit=list?ng_scroll_max(ng_raster_count(sh,library),library):0;
    uint64_t previous_signature=signature;
    bool edge_changed=false;
    if(list&&!library){
        const uint8_t edges=(uint8_t)((offset==0?1U:0U)|(offset==limit?2U:0U));
        const uint8_t previous_edges=(uint8_t)((previous==0?1U:0U)|(previous==limit?2U:0U));
        /* Keep endpoint state in the current slot context. Deriving the prior
         * context from the same stationary base proves that only these two
         * explicit footer controls changed during the translation. */
        previous_signature=home_cache_hash_bytes(signature,&previous_edges,sizeof(previous_edges));
        signature=home_cache_hash_bytes(signature,&edges,sizeof(edges));
        edge_changed=edges!=previous_edges;
    }
    const bool reuse=cached&&eligible&&sh->native_home_cache_valid&&sh->native_home_cache_pixels==(uintptr_t)pixels&&sh->native_home_cache_stride==stride;
    const bool same=reuse&&sh->ng_frame_structure_signature==structure&&sh->ng_frame_content_signature==content;
    const bool tagged=list&&eligible&&ng_raster_count(sh,library)>0;
    const bool scroll=physical_scroll&&tagged&&same&&previous_signature==sh->native_home_cache_signature&&
        magnitude>0&&magnitude<bottom-top;
    ng_scene_t scene={.shell=sh,.pixels=pixels,.stride=stride};
    if(cached&&eligible&&loading&&!sh->press_active&&same&&!sh->native_logical_incomplete&&
       signature==sh->native_home_cache_signature){
        ng_rect(&scene,NG_LOADING_X,NG_LOADING_Y,NG_LOADING_W,NG_LOADING_H,NG_SURFACE);
        ng_launch_progress(&scene);
        sh->native_update=(console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_REGION,
            .x=NG_LOADING_X,.y=NG_LOADING_Y,.width=NG_LOADING_W,.height=NG_LOADING_H};
        ++sh->native_home_dynamic_frames;
    }else if(sh->page==CONSOLE_PAGE_EXTERNAL){
        /* A static launch identity/chrome change, missing game or inactive
         * progress must reconstruct the complete non-interactive cover. */
        ng_build_scene(&scene);
        sh->native_update=(console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_FULL};
        sh->native_logical_incomplete=false;++sh->native_home_full_frames;
    }else if(scroll){
        const int first=shift<0?bottom-magnitude:top,last=shift<0?bottom:top+magnitude;
        if(ng_raster_covers(sh,library))ng_copy_raster_strip(sh,&scene,library,first,last);
        else{
            scene.clip_top=first;scene.clip_bottom=last;ng_rect(&scene,264,first,964,last-first,NG_INK);
            size_t games[CONSOLE_SHELL_MAX_APPS];const size_t count=library?ng_library_games(sh,games):sh->files.entry_count;
            if(library)ng_library_rows(&scene,games,count);else ng_file_rows(&scene,count);
            scene.clip_top=0;scene.clip_bottom=0;
        }
        ng_rect(&scene,1228,top,32,bottom-top,NG_INK);ng_scrollbar(&scene,ng_raster_count(sh,library),library);
        sh->native_update=(console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_SCROLL,
            .x=264,.y=(uint16_t)top,.width=996,.height=(uint16_t)(bottom-top)};
        if(edge_changed){
            ng_file_scroll_buttons(&scene);
            sh->native_update.scroll_stationary_x=368;sh->native_update.scroll_stationary_y=558;
            sh->native_update.scroll_stationary_width=288;sh->native_update.scroll_stationary_height=88;
        }
        sh->native_logical_incomplete=true;++sh->native_home_scroll_blit_frames;
    }else if(physical_scroll&&same&&signature==sh->native_home_cache_signature&&(!list||!magnitude)){
        /* The selected physical frame is already current. Keep stale logical
         * interior pixels private until an ordinary render needs them. */
        sh->native_update=(console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_NONE};
    }else if(sh->native_logical_incomplete){
        /* A failed/unsupported physical translation cannot submit stale
         * logical interior pixels, even when the stationary context matches. */
        ng_scene_t changed={.shell=sh};
        if(physical_scroll&&same&&!magnitude)
            ng_stationary_damage(sh,&changed,&controls,chrome,focus_ms,visible_press,visible_pressed,list,top);
        if(cached&&tagged&&ng_raster_covers(sh,library)){
            scene.skip_rows=true;ng_build_scene(&scene);ng_copy_raster(sh,&scene,library);
        }else ng_build_scene(&scene);
        sh->native_update=changed.damage_right>changed.damage_left?
            (console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_REGION,
                .x=(uint16_t)changed.damage_left,.y=(uint16_t)changed.damage_top,
                .width=(uint16_t)(changed.damage_right-changed.damage_left),.height=(uint16_t)(changed.damage_bottom-changed.damage_top)}:
            (console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_FULL};
        sh->native_logical_incomplete=false;++sh->native_home_full_frames;
    }else if(same&&signature==sh->native_home_cache_signature&&(!list||!magnitude)){
        sh->native_update=(console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_NONE};
    }else if(same){
        ng_stationary_damage(sh,&scene,&controls,chrome,focus_ms,visible_press,visible_pressed,list,top);
        if(list&&magnitude){
            ng_damage(&scene,264,top,996,bottom-top);
            if(!library){
                if((previous==0)!=(offset==0)||(previous==limit)!=(offset==limit))ng_damage(&scene,264,548,988,152);}
        }
        const bool intersects_list=list&&scene.damage_right>264&&scene.damage_left<1228&&scene.damage_top<bottom&&scene.damage_bottom>top;
        if(intersects_list&&ng_raster_count(sh,library)&&ng_raster_covers(sh,library)){
            scene.skip_rows=true;ng_build_scene(&scene);
            const int left=scene.damage_left>264?scene.damage_left:264,right=scene.damage_right<1228?scene.damage_right:1228;
            const int first=scene.damage_top>top?scene.damage_top:top,last=scene.damage_bottom<bottom?scene.damage_bottom:bottom;
            ng_copy_raster_region(sh,&scene,library,left,first,right,last);
            if(magnitude)++sh->native_home_scroll_blit_frames;
        }else if(list&&magnitude>0&&magnitude<bottom-top&&signature==sh->native_home_cache_signature&&
            (library||(previous>0&&offset>0&&previous<ng_scroll_max(sh->files.entry_count,false)&&offset<ng_scroll_max(sh->files.entry_count,false)))){
            const size_t bytes=964U*sizeof(*pixels);
            if(shift<0)for(int y=top;y<bottom-magnitude;++y)memmove(pixels+(size_t)y*stride+264U,pixels+(size_t)(y+magnitude)*stride+264U,bytes);
            else for(int y=bottom-1;y>=top+magnitude;--y)memmove(pixels+(size_t)y*stride+264U,pixels+(size_t)(y-magnitude)*stride+264U,bytes);
            scene.clip_top=shift<0?bottom-magnitude:top;scene.clip_bottom=shift<0?bottom:top+magnitude;
            ng_rect(&scene,264,scene.clip_top,964,scene.clip_bottom-scene.clip_top,NG_INK);
            size_t games[CONSOLE_SHELL_MAX_APPS];const size_t count=library?ng_library_games(sh,games):sh->files.entry_count;
            if(library)ng_library_rows(&scene,games,count);else ng_file_rows(&scene,count);
            scene.clip_top=0;scene.clip_bottom=0;ng_rect(&scene,1230,top,30,bottom-top,NG_INK);ng_scrollbar(&scene,count,library);
            ++sh->native_home_scroll_blit_frames;sh->native_home_shifted_pixels+=(uint64_t)(unsigned)(bottom-top-magnitude)*964U;
        }else if(scene.damage_right>scene.damage_left)ng_build_scene(&scene);
        sh->native_update=scene.damage_right>scene.damage_left?
            (console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_REGION,.x=(uint16_t)scene.damage_left,.y=(uint16_t)scene.damage_top,
                .width=(uint16_t)(scene.damage_right-scene.damage_left),.height=(uint16_t)(scene.damage_bottom-scene.damage_top)}:
            (console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_NONE};
        ++sh->native_home_dynamic_frames;
    }else{
        if(cached&&tagged&&ng_raster_covers(sh,library)){
            scene.skip_rows=true;ng_build_scene(&scene);ng_copy_raster(sh,&scene,library);
        }else ng_build_scene(&scene);
        sh->native_update=(console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_FULL};++sh->native_home_full_frames;
    }
    if(tagged){
        sh->native_update.scroll_context_valid=true;sh->native_update.scroll_context=signature;
        sh->native_update.previous_scroll_context=scroll?previous_signature:signature;
        sh->native_update.previous_scroll_offset=reuse?previous:offset;sh->native_update.current_scroll_offset=offset;
        sh->native_update.viewport_x=264;sh->native_update.viewport_y=(uint16_t)top;
        sh->native_update.viewport_width=964;sh->native_update.viewport_height=(uint16_t)(bottom-top);
    }
    sh->native_home_cache_valid=eligible;sh->native_home_cache_pixels=(uintptr_t)pixels;sh->native_home_cache_stride=stride;
    sh->native_home_cache_signature=signature;sh->native_home_cache_scroll_q16=offset;
    sh->ng_frame_structure_signature=structure;sh->ng_frame_content_signature=content;sh->ng_frame_chrome_signature=chrome;
    sh->ng_previous_focus=sh->ng_focus;sh->ng_previous_focus_ms=focus_ms;sh->ng_previous_pressed_index=visible_pressed;
    sh->ng_previous_press_active=visible_press;sh->ng_previous_file_selected=sh->file_selected_index;
    sh->dirty=false;++sh->render_generation;return true;
}
