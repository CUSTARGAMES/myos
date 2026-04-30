#include <stdint.h>
extern uint8_t *fb; extern int fp,fw,fh,mx,my,mb,mc; extern uint8_t md[3];
extern void outb(uint16_t,uint8_t); extern uint8_t inb(uint16_t);
#define pp(x,y,c) if((x)>=0&&(x)<fw&&(y)>=0&&(y)<fh)fb[(y)*fp+(x)]=c
#define BLACK 0
#define WHITE 15
#define LGRAY 7
#define DGRAY 8
#define LBLUE 9
#define CYAN 3
#define RED 4

static const uint8_t font[95][8]={
{0,0,0,0,0,0,0,0},{24,60,60,24,24,0,24,0},{102,102,36,0,0,0,0,0},{108,108,254,108,254,108,108,0},{24,62,96,60,6,124,24,0},{0,102,172,216,54,106,214,0},{56,108,56,118,220,204,118,0},{24,24,48,0,0,0,0,0},{12,24,48,48,48,24,12,0},{48,24,12,12,12,24,48,0},{0,102,60,255,60,102,0,0},{0,24,24,126,24,24,0,0},{0,0,0,0,0,24,24,48},{0,0,0,126,0,0,0,0},{0,0,0,0,0,24,24,0},{6,12,24,48,96,192,128,0},{124,198,206,222,246,230,124,0},{24,56,120,24,24,24,126,0},{124,198,6,28,48,102,254,0},{124,198,6,60,6,198,124,0},{28,60,108,204,254,12,12,0},{254,192,252,6,6,198,124,0},{56,96,192,252,198,198,124,0},{254,198,12,24,48,48,48,0},{124,198,198,124,198,198,124,0},{124,198,198,126,6,12,120,0},{0,24,24,0,0,24,24,0},{0,24,24,0,0,24,24,48},{0,0,0,126,0,0,126,0,0},{0,0,0,0,0,0,0,0},
{124,198,222,222,222,192,120,0},{56,108,198,254,198,198,198,0},{252,102,102,124,102,102,252,0},{60,102,192,192,192,102,60,0},{248,108,102,102,102,108,248,0},{254,98,104,120,104,98,254,0},{254,98,104,120,104,96,240,0},{60,102,192,222,198,102,58,0},{198,198,198,254,198,198,198,0},{60,24,24,24,24,24,60,0},{30,12,12,12,204,204,120,0},{230,102,108,120,108,102,230,0},{240,96,96,96,98,102,254,0},{198,238,254,214,198,198,198,0},{198,230,246,222,206,198,198,0},{124,198,198,198,198,198,124,0},{252,102,102,124,96,96,240,0},{124,198,198,198,198,214,124,6},{252,102,102,124,108,102,230,0},{124,198,96,56,12,198,124,0},{126,90,24,24,24,24,60,0},{198,198,198,198,198,198,124,0},{198,198,198,198,108,56,16,0},{198,198,198,214,254,238,198,0},{198,198,108,56,56,108,198,0},{102,102,102,60,24,24,60,0},{254,198,140,24,50,102,254,0},{60,48,48,48,48,48,60,0},{192,96,48,24,12,6,2,0},{60,12,12,12,12,12,60,0},{16,56,108,198,0,0,0,0},{0,0,0,0,0,0,0,255}};

static void dc(int x,int y,char c,uint8_t fg,uint8_t bg){
    if(c<32||c>126)return;
    const uint8_t *g=font[c-32];
    for(int r=0;r<8;r++){uint8_t b=g[r];for(int cc=0;cc<8;cc++)pp(x+cc,y+r,(b&(0x80>>cc))?fg:bg);}
}
static void ds(int x,int y,const char*s,uint8_t fg,uint8_t bg){while(*s){dc(x,y,*s++,fg,bg);x+=8;}}
static void fr(int x,int y,int w,int h,uint8_t c){for(int dy=0;dy<h;dy++)for(int dx=0;dx<w;dx++)pp(x+dx,y+dy,c);}
static void hl(int x,int y,int w,uint8_t c){for(int i=0;i<w;i++)pp(x+i,y,c);}
static void vl(int x,int y,int h,uint8_t c){for(int i=0;i<h;i++)pp(x,y+i,c);}
static void rbox(int x,int y,int w,int h){hl(x,y,w,WHITE);vl(x,y,h,WHITE);hl(x,y+h-1,w,DGRAY);vl(x+w-1,y,h,DGRAY);}
static int sl(const char*s){int n=0;while(s[n])n++;return n;}
static int sc(const char*a,const char*b){while(*a&&*b&&*a==*b){a++;b++;}return*a-*b;}

static int nbl=0,nbpos=0;
static char nbuf[2000];
static int winx=70,winy=40,winw=320,winh=200;
static int wvis=0;
static int err=0,ert=0,erx,ery;
static int curbg[484];
static int so=0;

static void draw_cur(void){
    for(int dy=0;dy<22;dy++)for(int dx=0;dx<22;dx++)if((dy+dx)<22&&dy>=dx){pp(mx+dx,my+dy,WHITE);pp(mx+dx+1,my+dy,WHITE);}
}
static void save_cur(void){int idx=0;for(int dy=0;dy<22;dy++)for(int dx=0;dx<22;dx++){int px=mx+dx,py=my+dy;curbg[idx++]=(px>=0&&px<fw&&py>=0&&py<fh)?fb[py*fp+px]:0;}}
static void rest_cur(void){int idx=0;for(int dy=0;dy<22;dy++)for(int dx=0;dx<22;dx++){int px=mx+dx,py=my+dy;if(px>=0&&px<fw&&py>=0&&py<fh)fb[py*fp+px]=curbg[idx];idx++;}}

static void draw_win(void){
    if(!wvis)return;
    fr(winx,winy,winw,winh,WHITE);
    fr(winx,winy,winw,18,LBLUE);
    ds(winx+4,winy+2,"Notepad - Untitled",WHITE,LBLUE);
    int bx=winx+winw-18;
    fr(bx,winy,16,16,LGRAY);rbox(bx,winy,16,16);dc(bx+4,winy+2,'X',BLACK,LGRAY);
    rbox(winx,winy+18,winw,winh-18);
    int cx=winx+4,cy=winy+22,mc=(winw-8)/8,mr=(winh-40)/8;
    fr(cx,cy,mc*8,mr*8,WHITE);
    hl(cx,cy,mc*8,DGRAY);vl(cx,cy,mr*8,DGRAY);hl(cx,cy+mr*8-1,mc*8,WHITE);vl(cx+mc*8-1,cy,mr*8,WHITE);
    int pos=0,row=0;
    while(row<mr&&pos<nbl){
        int col=0;
        while(col<mc&&pos<nbl&&nbuf[pos]!='\n'){dc(cx+col*8,cy+row*8,nbuf[pos],BLACK,WHITE);col++;pos++;}
        if(pos<nbl&&nbuf[pos]=='\n')pos++;
        row++;
    }
}

static void show_err(void){
    erx=(fw-200)/2;ery=(fh-70)/2;
    fr(erx,ery,200,70,LGRAY);rbox(erx,ery,200,70);
    fr(erx,ery,200,18,RED);ds(erx+4,ery+2,"Error",WHITE,RED);
    ds(erx+20,ery+30,"No filesystem found!",BLACK,LGRAY);
    int bx=erx+80;fr(bx,ery+48,40,16,LGRAY);rbox(bx,ery+48,40,16);ds(bx+10,ery+50,"OK",BLACK,LGRAY);
    err=1;ert=300000;
}

static void draw_taskbar(void){
    int tbh=24;
    fr(0,fh-tbh,fw,tbh,LGRAY);hl(0,fh-tbh,fw,WHITE);
    fr(2,fh-tbh+2,56,tbh-4,LGRAY);rbox(2,fh-tbh+2,56,tbh-4);ds(10,fh-tbh+4,"Start",BLACK,LGRAY);
    int cx=fw-56;fr(cx,fh-tbh+2,52,tbh-4,LGRAY);hl(cx,fh-tbh+2,52,DGRAY);vl(cx,fh-tbh+2,tbh-4,DGRAY);hl(cx,fh-tbh+tbh-3,52,WHITE);vl(cx+51,fh-tbh+2,tbh-4,WHITE);
    ds(cx+12,fh-tbh+4,"12:00",BLACK,LGRAY);
}

static void draw_icon(int x,int y,const char*label){
    fr(x+2,y+2,28,28,WHITE);rbox(x+2,y+2,28,28);
    fr(x+4,y+4,24,8,LBLUE);hl(x+6,y+16,20,DGRAY);hl(x+6,y+19,14,DGRAY);hl(x+6,y+22,16,DGRAY);
    int len=sl(label);int tx=x+(32-len*8)/2;
    for(int i=0;i<len;i++)dc(tx+i*8,y+34,label[i],WHITE,CYAN);
}

void gui(void){
    mx=fw/2;my=fh/2;save_cur();draw_cur();
    int tick=0,pb=0;
    while(1){
        while(inb(0x64)&1){
            uint8_t st=inb(0x64),d=inb(0x60);
            if(st&0x20){
                if(mc==0){if(d&0x08){md[0]=d;mc++;}}
                else{md[mc++]=d;if(mc==3){mc=0;
                    int dx=md[1],dy=md[2];
                    if(md[0]&0x10)dx|=~0xFF;if(md[0]&0x20)dy|=~0xFF;dy=-dy;
                    mb=md[0]&0x07;
                    mx+=dx;my+=dy;
                    if(mx<0)mx=0;if(my<0)my=0;if(mx>=fw)mx=fw-1;if(my>=fh)my=fh-1;
                    int clk=(pb==0&&mb==1);pb=mb;
                    if(clk){
                        if(mx>=2&&mx<58&&my>=fh-22&&my<fh-2)so=!so;
                        if(mx>=20&&mx<52&&my>=20&&my<60){wvis=!wvis;if(wvis)winx=80;winy=40;so=0;}
                        if(mx>=20&&mx<52&&my>=100&&my<140){show_err();so=0;}
                        if(mx>=20&&mx<52&&my>=180&&my<220){show_err();so=0;}
                        if(err&&mx>=erx+80&&mx<erx+120&&my>=ery+48&&my<ery+64)err=0;
                        if(so&&mx>=2&&mx<158&&my>=fh-2-24-4-96&&my<fh-2-4){
                            int iy=fh-2-24-4-96;
                            if(my>=iy&&my<iy+24){so=0;wvis=1;winx=80;winy=40;}
                            else if(my>=iy+24&&my<iy+72){so=0;show_err();}
                            else if(my>=iy+72&&my<iy+96){so=0;show_err();}
                        }
                        if(wvis&&mx>=winx+winw-18&&mx<winx+winw-2&&my>=winy&&my<winy+16)wvis=0;
                    }
                }}
            }else{
                char c=0;
                switch(d){case 0x0E:c='\b';break;case 0x1C:c='\n';break;case 0x39:c=' ';break;
                    case 0x02:c='1';break;case 0x03:c='2';break;case 0x04:c='3';break;case 0x05:c='4';break;
                    case 0x06:c='5';break;case 0x07:c='6';break;case 0x08:c='7';break;case 0x09:c='8';break;
                    case 0x0A:c='9';break;case 0x0B:c='0';break;case 0x0C:c='-';break;case 0x0D:c='=';break;
                    case 0x10:c='q';break;case 0x11:c='w';break;case 0x12:c='e';break;case 0x13:c='r';break;
                    case 0x14:c='t';break;case 0x15:c='y';break;case 0x16:c='u';break;case 0x17:c='i';break;
                    case 0x18:c='o';break;case 0x19:c='p';break;case 0x1A:c='[';break;case 0x1B:c=']';break;
                    case 0x1E:c='a';break;case 0x1F:c='s';break;case 0x20:c='d';break;case 0x21:c='f';break;
                    case 0x22:c='g';break;case 0x23:c='h';break;case 0x24:c='j';break;case 0x25:c='k';break;
                    case 0x26:c='l';break;case 0x27:c=';';break;case 0x28:c='\'';break;case 0x29:c='`';break;
                    case 0x2B:c='\\';break;case 0x2C:c='z';break;case 0x2D:c='x';break;case 0x2E:c='c';break;
                    case 0x2F:c='v';break;case 0x30:c='b';break;case 0x31:c='n';break;case 0x32:c='m';break;
                    case 0x33:c=',';break;case 0x34:c='.';break;case 0x35:c='/';break;case 0x37:c='*';break;
                }
                if(c=='\b'){if(nbl>0)nbl--;}
                else if(c=='\n'){if(nbl<1999)nbuf[nbl++]='\n';}
                else if(c>=32&&c<=126){if(nbl<1999)nbuf[nbl++]=c;}
            }
        }
        rest_cur();
        fr(0,0,fw,fh-24,CYAN);
        draw_icon(20,20,"Notepad");draw_icon(20,100,"My PC");draw_icon(20,180,"Calc");
        draw_taskbar();
        if(so){int sy=fh-24-4*24-4;fr(2,sy,160,4*24+4,LGRAY);rbox(2,sy,160,4*24+4);ds(10,sy+4,"Notepad",BLACK,LGRAY);ds(10,sy+28,"My PC",BLACK,LGRAY);ds(10,sy+52,"Calculator",BLACK,LGRAY);ds(10,sy+76,"Shut Down",BLACK,LGRAY);}
        draw_win();
        if(err){fr(erx,ery,200,70,LGRAY);rbox(erx,ery,200,70);fr(erx,ery,200,18,RED);ds(erx+4,ery+2,"Error",WHITE,RED);ds(erx+20,ery+30,"No filesystem found!",BLACK,LGRAY);int bx=erx+80;fr(bx,ery+48,40,16,LGRAY);rbox(bx,ery+48,40,16);ds(bx+10,ery+50,"OK",BLACK,LGRAY);ert--;if(ert<=0)err=0;}
        save_cur();draw_cur();
        tick++;if(!err&&(tick%4000000==0))show_err();
        for(volatile int dly=0;dly<500;dly++);
    }
}
