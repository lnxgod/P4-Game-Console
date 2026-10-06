// SPDX-License-Identifier: MIT
#include "console/startup.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "flight_fonts.h"

/* Stateless, allocation-free projection. The host and tablet draw identical
 * frames from elapsed time; no sequence must finish before the desktop opens. */
typedef struct {uint16_t *p;size_t stride,w,h;} flight_canvas_t;
static uint16_t color(unsigned r,unsigned g,unsigned b)
{return (uint16_t)(((r>>3U)<<11U)|((g>>2U)<<5U)|(b>>3U));}
static uint16_t blend(uint16_t a,uint16_t b,unsigned opacity)
{
    const unsigned inv=255U-opacity;
    const unsigned r=(((unsigned)a>>11U)*opacity+((unsigned)b>>11U)*inv+127U)/255U;
    const unsigned g=((((unsigned)a>>5U)&63U)*opacity+(((unsigned)b>>5U)&63U)*inv+127U)/255U;
    const unsigned blue=(((unsigned)a&31U)*opacity+((unsigned)b&31U)*inv+127U)/255U;
    return (uint16_t)((r<<11U)|(g<<5U)|blue);
}
static int clampi(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static void fill(flight_canvas_t *c,int x,int y,int width,int height,uint16_t value)
{
    const int x0=clampi(x*(int)c->w/1280,0,(int)c->w);
    const int y0=clampi(y*(int)c->h/720,0,(int)c->h);
    const int x1=clampi((x+width)*(int)c->w/1280,0,(int)c->w);
    const int y1=clampi((y+height)*(int)c->h/720,0,(int)c->h);
    for(int py=y0;py<y1;++py)for(int px=x0;px<x1;++px)c->p[(size_t)py*c->stride+(size_t)px]=value;
}
static void background(flight_canvas_t *c,int top,int bottom,bool full)
{
    const size_t y0=(size_t)top*c->h/720U,y1=(size_t)bottom*c->h/720U;
    for(size_t y=y0;y<y1;++y){
        const unsigned ly=(unsigned)(y*720U/c->h);
        const int vertical=abs((int)ly-250)*2;
        const size_t left=full?0U:256U*c->w/1280U;
        const size_t right=full?c->w:1024U*c->w/1280U;
        for(size_t x=left;x<right;){
            const int lx=(int)((x/16U*16U)*1280U/c->w);
            const int distance=abs(lx-640)+vertical;
            const unsigned glow=distance<540?(unsigned)(540-distance)/36U:0U;
            const uint16_t value=color(5U,13U+glow,23U+glow);
            const size_t boundary=(x/16U+1U)*16U;
            const size_t end=boundary<right?boundary:right;
            for(size_t px=x;px<end;++px)c->p[y*c->stride+px]=value;
            x=end;
        }
    }
}
static void dot(flight_canvas_t *c,float x,float y,int radius,uint16_t value)
{
    const int left=(int)x-radius,top=(int)y-radius;
    for(int row=0;row<radius*2;++row)for(int col=0;col<radius*2;++col){
        const int dx=col-radius,dy=row-radius;
        if(dx*dx+dy*dy<=radius*radius)fill(c,left+col,top+row,1,1,value);
    }
}
static void line(flight_canvas_t *c,int x,int y,int x1,int y1,uint16_t value)
{
    const int dx=abs(x1-x),dy=-abs(y1-y),sx=x<x1?1:-1,sy=y<y1?1:-1;
    int error=dx+dy;
    for(;;){fill(c,x,y,2,2,value);if(x==x1&&y==y1)break;
        const int e=2*error;if(e>=dy){error+=dy;x+=sx;}if(e<=dx){error+=dx;y+=sy;}}
}
/* Fixed orbital path avoids hundreds of transcendental calls per frame. */
static const int16_t orbit_points[180][2]={
    {892,395},
    {892,396},
    {891,398},
    {891,399},
    {890,401},
    {888,402},
    {886,404},
    {885,405},
    {882,407},
    {880,408},
    {877,409},
    {874,411},
    {870,412},
    {866,413},
    {863,415},
    {858,416},
    {854,417},
    {849,418},
    {844,420},
    {839,421},
    {833,422},
    {827,423},
    {821,424},
    {815,425},
    {809,426},
    {802,427},
    {795,428},
    {788,429},
    {781,430},
    {774,431},
    {766,431},
    {758,432},
    {750,433},
    {742,433},
    {734,434},
    {726,434},
    {718,435},
    {709,435},
    {701,436},
    {692,436},
    {684,436},
    {675,437},
    {666,437},
    {658,437},
    {649,437},
    {640,437},
    {631,437},
    {622,437},
    {614,437},
    {605,437},
    {596,436},
    {588,436},
    {579,436},
    {571,435},
    {562,435},
    {554,434},
    {546,434},
    {538,433},
    {530,433},
    {522,432},
    {514,431},
    {506,431},
    {499,430},
    {492,429},
    {485,428},
    {478,427},
    {471,426},
    {465,425},
    {459,424},
    {453,423},
    {447,422},
    {441,421},
    {436,420},
    {431,418},
    {426,417},
    {422,416},
    {417,415},
    {414,413},
    {410,412},
    {406,411},
    {403,409},
    {400,408},
    {398,407},
    {395,405},
    {394,404},
    {392,402},
    {390,401},
    {389,399},
    {389,398},
    {388,396},
    {388,395},
    {388,394},
    {389,392},
    {389,391},
    {390,389},
    {392,388},
    {394,386},
    {395,385},
    {398,383},
    {400,382},
    {403,381},
    {406,379},
    {410,378},
    {414,377},
    {417,375},
    {422,374},
    {426,373},
    {431,372},
    {436,370},
    {441,369},
    {447,368},
    {453,367},
    {459,366},
    {465,365},
    {471,364},
    {478,363},
    {485,362},
    {492,361},
    {499,360},
    {506,359},
    {514,359},
    {522,358},
    {530,357},
    {538,357},
    {546,356},
    {554,356},
    {562,355},
    {571,355},
    {579,354},
    {588,354},
    {596,354},
    {605,353},
    {614,353},
    {622,353},
    {631,353},
    {640,353},
    {649,353},
    {658,353},
    {666,353},
    {675,353},
    {684,354},
    {692,354},
    {701,354},
    {709,355},
    {718,355},
    {726,356},
    {734,356},
    {742,357},
    {750,357},
    {758,358},
    {766,359},
    {774,359},
    {781,360},
    {788,361},
    {795,362},
    {802,363},
    {809,364},
    {815,365},
    {821,366},
    {827,367},
    {833,368},
    {839,369},
    {844,370},
    {849,372},
    {854,373},
    {858,374},
    {863,375},
    {866,377},
    {870,378},
    {874,379},
    {877,381},
    {880,382},
    {882,383},
    {885,385},
    {886,386},
    {888,388},
    {890,389},
    {891,391},
    {891,392},
    {892,394}
};
static void orbit(flight_canvas_t *c,uint32_t ms,bool front,bool complete)
{
    const float phase=complete?0.0f:(float)(ms%7000U)*6.2831853f/7000.0f;
    for(int i=0;i<180;++i){
        if((i>0&&i<90)!=front)continue;
        dot(c,(float)orbit_points[i][0],(float)orbit_points[i][1],1,color(17,58,72));
    }
    for(unsigned i=0;i<3U;++i){
        const float a=phase+(float)i*2.0943951f;
        if((sinf(a)>0.0f)!=front)continue;
        for(int tail=9;tail>=0;--tail){
            const float t=a-(float)tail*.037f;
            const unsigned light=(unsigned)(10-tail);
            dot(c,640.0f+252.0f*cosf(t),395.0f+42.0f*sinf(t),tail==0?4:2,
                color(18U+light*5U,58U+light*16U,75U+light*16U));
        }
    }
}
static int text_width(const char *text,bool title)
{
    const char *chars=title?flight_font56_chars:flight_font32_chars;
    const uint8_t *advance=title?flight_font56_advance:flight_font32_advance;
    int width=0;
    for(size_t i=0;i<80U&&text[i];++i){
        const char *found=strchr(chars,text[i]);
        if(found)width+=advance[(size_t)(found-chars)];
    }
    return width;
}
static void text(flight_canvas_t *c,int x,int y,const char *label,bool title,uint16_t value,int max_width)
{
    const char *chars=title?flight_font56_chars:flight_font32_chars;
    const uint8_t *advance=title?flight_font56_advance:flight_font32_advance;
    const uint8_t *atlas=title?flight_font56_alpha:flight_font32_alpha;
    const unsigned cell=title?64U:40U;const int limit=x+max_width;
    for(size_t n=0;n<80U&&label[n];++n){
        const char *found=strchr(chars,label[n]);
        if(!found)found=strchr(chars,'?');
        if(!found)continue;
        const size_t glyph=(size_t)(found-chars);
        if(x+(int)advance[glyph]>limit)break;
        const int px0=x*(int)c->w/1280,py0=y*(int)c->h/720;
        const unsigned pw=cell*(unsigned)c->w/1280U,ph=cell*(unsigned)c->h/720U;
        for(unsigned yy=0;yy<ph;++yy)for(unsigned xx=0;xx<pw;++xx){
            const int px=px0+(int)xx,py=py0+(int)yy;
            if(px<0||py<0||px>=(int)c->w||py>=(int)c->h||px>=limit*(int)c->w/1280)continue;
            const size_t i=glyph*cell*cell+(yy*cell/ph)*cell+xx*cell/pw;
            const unsigned a=(i&1U)?atlas[i/2U]&15U:atlas[i/2U]>>4U;
            if(a){uint16_t *dst=c->p+(size_t)py*c->stride+(size_t)px;*dst=blend(value,*dst,a*17U);}
        }
        x+=advance[glyph];
    }
}
static void centered(flight_canvas_t *c,int y,const char *label,bool title,uint16_t value)
{
    int width=text_width(label,title);if(width>1136)width=1136;
    text(c,(1280-width)/2,y,label,title,value,1136);
}
static float smooth(float t){return t*t*(3.0f-2.0f*t);}
/* Q10 texture coordinate using bounded 32-bit native arithmetic. */
static int32_t texture_coordinate(int32_t numerator,int32_t denominator)
{
    const int32_t whole=numerator/denominator;
    return whole*1024+(numerator-whole*denominator)*1024/denominator;
}
static void project_mark(flight_canvas_t *c,const uint8_t *logo,uint32_t ms,bool complete)
{
    float t=complete?1.0f:(float)(ms<1500U?ms:1500U)/1500.0f;
    const float ease=1.0f-(1.0f-t)*(1.0f-t)*(1.0f-t);
    const float hover=ms>1500U&&!complete?sinf((float)(ms%6200U)*6.2831853f/6200.0f):0.0f;
    const float cx=640.0f-210.0f*(1.0f-ease),cy=252.0f-55.0f*sinf(t*3.1415927f)+hover*5.0f;
    const float side=410.0f*(.52f+.48f*ease);
    const float yaw=(-1.12f*(1.0f-ease)+.22f*sinf(t*6.2831853f)*(1.0f-t)+hover*.07f);
    const float roll=-.30f*(1.0f-ease)+.13f*sinf(t*3.1415927f)*(1.0f-t);
    const float a=cosf(roll)*cosf(yaw),b=-sinf(roll),d=sinf(roll)*cosf(yaw),e=cosf(roll),z=-sinf(yaw);
    const float depth=850.0f,base=a*e-b*d,invside=1.0f/side;
    const float xscale=1280.0f/(float)c->w,yscale=720.0f/(float)c->h;
    const unsigned opacity=(unsigned)(150.0f+105.0f*smooth(t));
    /* The projected sprite is a real perspective plane with homogeneous
     * interpolation. Alpha stays transparent; no rectangle or fake 3D box. */
    const int x0=clampi((int)((cx-side*.72f)*(float)c->w/1280.0f),0,(int)c->w);
    const int x1=clampi((int)((cx+side*.72f)*(float)c->w/1280.0f)+1,0,(int)c->w);
    const int y0=clampi((int)((cy-side*.72f)*(float)c->h/720.0f),0,(int)c->h);
    const int y1=clampi((int)((cy+side*.72f)*(float)c->h/720.0f)+1,0,(int)c->h);
    /* Homogeneous coordinates stay fixed-point through each scanline.
     * Eight-pixel spans interpolate perspective-correct endpoints; the
     * pixel loop only advances fixed coordinates. 20 fractional bits retain
     * the projection while Q10 texels bound interpolation rounding. */
    const float coordinate_scale=1048576.0f;
    const float texture_scale=coordinate_scale*384.0f;
    const float dx=xscale;
    const float determinant_step=-z*e*dx/depth;
    const int32_t dq=(int32_t)(determinant_step*coordinate_scale);
    const int32_t du=(int32_t)((e*dx*invside+.5f*determinant_step)*texture_scale);
    const int32_t dv=(int32_t)((-d*dx*invside+.5f*determinant_step)*texture_scale);
    const float left=(float)x0*xscale-cx;
    for(int py=y0;py<y1;++py){
        const float y=(float)py*yscale-cy;
        const float determinant=base+z*(b*y-e*left)/depth;
        int32_t wq=(int32_t)(determinant*coordinate_scale);
        int32_t uq=(int32_t)(((left*e-b*y)*invside+.5f*determinant)*texture_scale);
        int32_t vq=(int32_t)(((a*y-d*left)*invside+.5f*determinant)*texture_scale);
        int32_t from_u=texture_coordinate(uq,wq),from_v=texture_coordinate(vq,wq);
        for(int px=x0;px<x1;){
            const int count=x1-px<8?x1-px:8;
            uq+=du*count;vq+=dv*count;wq+=dq*count;
            const int32_t to_u=texture_coordinate(uq,wq),to_v=texture_coordinate(vq,wq);
            const int32_t step_u=(to_u-from_u)/count,step_v=(to_v-from_v)/count;
            int32_t u=from_u,v=from_v;
            for(int n=0;n<count;++n,++px,u+=step_u,v+=step_v){
                if(u<0||v<0||u>=384*1024||v>=384*1024)continue;
                const size_t i=((size_t)((unsigned)v>>10U)*384U+
                                (size_t)((unsigned)u>>10U))*3U;
                const unsigned alpha=(unsigned)logo[i+2U]*opacity/255U;
                if(alpha){uint16_t *dst=c->p+(size_t)py*c->stride+(size_t)px;
                    *dst=blend((uint16_t)((unsigned)logo[i]|((unsigned)logo[i+1U]<<8U)),*dst,alpha);}
            }
            from_u=to_u;from_v=to_v;
        }
    }
}
bool console_startup_render_flight(uint16_t *pixels,size_t stride,size_t width,size_t height,
    const uint8_t *logo,size_t logo_bytes,uint32_t elapsed_ms,const char *status,bool complete,bool repaint)
{
    if(!pixels||!logo||logo_bytes!=CONSOLE_STARTUP_LOGO_BYTES||
       !((width==1280U&&height==720U)||(width==1152U&&height==720U)||(width==768U&&height==480U))||
       stride<width||stride>SIZE_MAX/height/sizeof(*pixels))return false;
    flight_canvas_t c={pixels,stride,width,height};
    const uint16_t white=color(244,250,252),cyan=color(85,230,236),muted=color(180,203,213);
    if(repaint){
        background(&c,0,720,true);
        centered(&c,477,"GameChangersAI",true,white);
        centered(&c,550,"OS " CONSOLE_PRODUCT_VERSION,false,cyan);
        line(&c,582,607,698,607,color(28,66,82));
    }
    background(&c,0,468,false);
    orbit(&c,elapsed_ms,false,complete);
    project_mark(&c,logo,elapsed_ms,complete);
    orbit(&c,elapsed_ms,true,complete);
    /* The footer never scrolls and never pretends to know a percentage.
     * Its fixed dots signal activity; only actual readiness lights all three. */
    background(&c,632,720,false);
    const char *label=status?status:"Starting...";
    const int label_width=clampi(text_width(label,false),0,736);
    text(&c,(1280-label_width)/2,642,label,false,muted,736);
    for(unsigned i=0;i<3U;++i){
        const bool active=complete||((elapsed_ms/240U)%3U)==i;
        dot(&c,622.0f+(float)i*18.0f,701.0f,3,active?cyan:color(29,64,77));
    }
    return true;
}
