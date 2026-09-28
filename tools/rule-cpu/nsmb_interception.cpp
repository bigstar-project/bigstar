// Exact double-precision counterpart of the existing item-planner rollout.
// No fast math/FMA, frame skipping, changed candidates, or new game heuristics.
#include <algorithm>
#include <cmath>
#include <cstdint>

struct State { double x, depth, vx, vy; int grounded, facing, turn, edge; };
struct Point { double x, depth; };
static bool occupied(double x, double y, const uint8_t* tiles, int top, int rows) {
    int tx = static_cast<int>(std::floor(x / 16)) % 64;
    if (tx < 0) tx += 64;
    int ty = static_cast<int>(std::floor(y / 16)) - top;
    return ty >= 0 && ty < rows && tiles[ty * 64 + tx] != 0;
}
static double ground(double vx, int direction, bool run) {
    if (!direction) return std::copysign(std::max(0.0, std::abs(vx)-.03515625),vx);
    double along = vx*direction;
    if (along < 0) return vx+direction*.078125;
    if (!run && along > 1.5) return direction*std::max(1.5,along-(along<2.25 ? .03125 : .0234375));
    double a = along<.5 ? .0703125 : along<1.5 ? (run ? .04296875 : .03515625) : along<2.25 ? .03125 : .0234375;
    return direction*std::min(run ? 3.0 : 1.5,along+a);
}
static bool step(State& s, int previous, int held, const uint8_t* tiles, int top, int rows) {
    int direction = ((held&16)!=0)-((held&32)!=0);
    bool jump = (held&3)!=0, run = (held&2048)!=0;
    if (s.grounded && jump && !(previous&3)) {
        double speed=std::abs(s.vx);
        s.vy=speed<1 ? 3.65625 : speed<1.5 ? 3.78125 : 3.90625;
        s.grounded=0;
    }
    if (s.grounded) {
        if (direction && direction!=s.facing) {
            s.vx=std::copysign(std::max(0.0,std::abs(s.vx)-.03515625),s.vx);
            s.facing=direction;s.turn=2;
        } else if (s.turn) {
            s.vx=std::copysign(std::max(0.0,std::abs(s.vx)-.078125),s.vx);--s.turn;
        } else if (direction && s.vx*direction<0) s.vx+=direction*.1875;
        else s.vx=ground(s.vx,direction,run);
    } else {
        double along=s.vx*direction;
        if (direction && run) {
            double a=along<.5 ? .0703125 : along<1.5 ? .04296875 : along<2.25 ? .03125 : .0234375;
            s.vx=direction*std::min(3.0,along+a);
        } else if (direction && along<1.5) s.vx=direction*std::min(1.5,along+(along<.5 ? .0703125 : .03515625));
        if (direction) {s.facing=direction;s.turn=0;}
    }
    double nx=s.x+s.vx;int side=s.vx>0 ? 1 : -1;
    if (s.vx && (occupied(nx+side*8,s.depth-4,tiles,top,rows) || occupied(nx+side*8,s.depth-11,tiles,top,rows))) {
        if (!s.grounded) return false;
        double boundary=std::floor((nx+side*8)/16)*16;
        nx=side>0 ? boundary-8 : boundary+24;s.vx=0;
    }
    s.x=nx;
    if (s.grounded) {
        if (occupied(s.x-5,s.depth+1,tiles,top,rows) || occupied(s.x,s.depth+1,tiles,top,rows) || occupied(s.x+4,s.depth+1,tiles,top,rows)) s.vy=-2;
        else {s.grounded=0;s.depth+=2.34375;s.vy=0;s.edge=4;}
    } else {
        double nd;
        if (s.edge) {nd=s.depth+.34375;s.vy=s.edge>=3 ? 0 : -.34375;--s.edge;}
        else {
            double a=jump && s.vy>2.5 ? -.0625 : ((jump && s.vy>1.5) || (-2<s.vy && s.vy<0)) ? -.25 : -.34375;
            s.vy=s.vy<-4 ? std::min(-4.0,s.vy-a) : std::max(-4.0,s.vy+a);
            nd=s.depth-s.vy;
        }
        if (s.vy>0) {
            for (int y=static_cast<int>(std::floor((s.depth-16)/16))*16; y>=std::ceil((nd-16)/16)*16; y-=16) {
                if (occupied(s.x-2,y-.01,tiles,top,rows) || occupied(s.x,y-.01,tiles,top,rows) || occupied(s.x+1,y-.01,tiles,top,rows)) {nd=y+16;s.vy=0;break;}
            }
        } else {
            for (int y=static_cast<int>(std::ceil(s.depth/16))*16; y<=std::floor(nd/16)*16; y+=16) {
                if (occupied(s.x-5,y+.01,tiles,top,rows) || occupied(s.x,y+.01,tiles,top,rows) || occupied(s.x+4,y+.01,tiles,top,rows)) {nd=y;s.vy=-2;s.grounded=1;break;}
            }
        }
        s.depth=nd;
    }
    return true;
}
static double wrap(double d) {double r=std::fmod(d+512,1024);return (r<0 ? r+1024 : r)-512;}

extern "C" __declspec(dllexport) int choose_interception(
    const State* initial, int previous, int mode, const uint8_t* tiles, int top, int rows,
    const int* inputs, int candidates, int stride, const int* preference, const int* duration,
    const Point* items, const uint8_t* collectable, int horizon,
    const Point* enemies, const int* enemy_lengths, int enemy_count, int* pickup) {
    int best=-1,best_frame=horizon+1;
    for (int candidate=0;candidate<candidates;++candidate) {
        State s=*initial;int prev=previous;
        for (int i=0;i<horizon;++i) {
            int held=inputs[candidate*stride+i];
            if (!step(s,prev,held,tiles,top,rows)) break;
            prev=held;
            if (s.depth>288 || (mode==0 && !s.grounded)) break;
            bool hazard=false;
            for (int e=0;e<enemy_count;++e) {
                const auto& p=enemies[e*horizon+i];
                if (i>=enemy_lengths[e] || (std::abs(wrap(s.x-p.x))<16 && std::abs(s.depth-p.depth)<24)) {hazard=true;break;}
            }
            if (hazard) break;
            if (collectable[i] && std::abs(wrap(s.x-items[i].x))<10 && std::abs(s.depth-items[i].depth)<10) {
                if (i+1<best_frame || (i+1==best_frame && mode==0 &&
                    (preference[candidate]<preference[best] || (preference[candidate]==preference[best] && duration[candidate]<duration[best])))) {
                    best=candidate;best_frame=i+1;
                }
                break;
            }
            if (i+1>=best_frame) break;
        }
    }
    *pickup=best_frame;return best;
}
