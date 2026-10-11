#ifndef ROCK_EXPANSION_POLICY_H
#define ROCK_EXPANSION_POLICY_H
#include <algorithm>
#include <cstdlib>
#include <array>
#include <vector>
#include <limits>
namespace RockExpansionPolicy {
struct Tile { bool rock=false, free=false, walkable=false, owned=false, unsafe=false; };
struct Site { int x=-1,y=-1,room=0,clearance=0,distance=0,baseDistance=0; bool valid() const { return x>=0; } };
// Safety and usable room are eligibility checks. Among eligible sites, main
// base distance is the first ranking key, never extra clearance or island size.
inline bool betterSite(const Site& candidate,const Site& best) {
    if(!best.valid()) return true;
    if(candidate.baseDistance!=best.baseDistance) return candidate.baseDistance<best.baseDistance;
    if(candidate.clearance!=best.clearance) return candidate.clearance>best.clearance;
    if(candidate.room!=best.room) return candidate.room>best.room;
    return candidate.distance<best.distance;
}
// Linear map survey: formations, reachable ground and summed local building
// space. No nested full-map searches or per-candidate pathfinding.
inline Site choose(int w,int h,const std::vector<Tile>& tiles,
                   const std::vector<int>& starts,const std::vector<int>& enemies,
                   const std::vector<int>& reserved, int mainBase=-1) {
    const int n=w*h;
    std::vector<int> component(n,-1),distance(n,-1),freeCount;
    std::vector<bool> occupied;
    std::vector<int> queue; queue.reserve(n);
    // Formations are edge-connected rock: a building needs contiguous ground.
    auto neighbours=[&](int i,auto visit) {
        if(i%w) visit(i-1); if(i%w+1<w) visit(i+1);
        if(i>=w) visit(i-w); if(i+w<n) visit(i+w);
    };
    // Ground units drive in eight directions and the engine's path search has
    // no corner rule, so a vehicle squeezes diagonally between two buildings.
    // Reachability has to match, or an MCV boxed in on its four sides reads as
    // stranded and is never sent anywhere while its diagonal is open.
    auto driveNeighbours=[&](int i,auto visit) {
        const int x=i%w,y=i/w;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if((dx||dy)&&x+dx>=0&&x+dx<w&&y+dy>=0&&y+dy<h) visit(i+dy*w+dx);
    };
    for(int i=0;i<n;++i) if(tiles[i].rock && component[i]<0) {
        const int id=int(freeCount.size()); freeCount.push_back(0);occupied.push_back(false);
        queue.clear();queue.push_back(i);component[i]=id;
        for(size_t j=0;j<queue.size();++j) {
            const int at=queue[j];freeCount[id]+=tiles[at].free;
            occupied[id]=occupied[id]||tiles[at].owned;
            neighbours(at,[&](int next) {if(tiles[next].rock&&component[next]<0){component[next]=id;queue.push_back(next);}});
        }
    }
    queue.clear();
    for(int i:starts) if(i>=0&&i<n&&tiles[i].walkable&&!tiles[i].unsafe&&distance[i]<0){distance[i]=0;queue.push_back(i);}
    for(size_t j=0;j<queue.size();++j) {
        const int at=queue[j];
        driveNeighbours(at,[&](int next){if(tiles[next].walkable&&!tiles[next].unsafe&&distance[next]<0){distance[next]=distance[at]+1;queue.push_back(next);}});
    }
    std::vector<int> sum((w+1)*(h+1));
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const int i=y*w+x;
        sum[(y+1)*(w+1)+x+1]=sum[y*(w+1)+x+1]+sum[(y+1)*(w+1)+x]-sum[y*(w+1)+x]
            +int(tiles[i].rock&&tiles[i].free&&!tiles[i].unsafe);
    }
    Site best;
    for(int y=0;y<h-1;++y) for(int x=0;x<w-1;++x) {
        const int i=y*w+x,c=component[i];
        if(c<0||occupied[c]||freeCount[c]<48||distance[i]<0)continue;
        bool valid=true;
        for(int d:{0,1,w,w+1}) if(!tiles[i+d].rock||!tiles[i+d].free||tiles[i+d].unsafe)valid=false;
        for(int r:reserved) if(component[r]==c)valid=false; // One MCV per new formation.
        if(!valid)continue;
        const int x0=std::max(0,x-6),x1=std::min(w,x+8),y0=std::max(0,y-6),y1=std::min(h,y+8);
        const int room=sum[y1*(w+1)+x1]-sum[y0*(w+1)+x1]-sum[y1*(w+1)+x0]+sum[y0*(w+1)+x0];
        if(room<48)continue;
        int clearance=w+h;
        for(int e:enemies) clearance=std::min(clearance,std::max(std::abs(x-e%w),std::abs(y-e/w)));
        if(clearance<12)continue;
        // Manhattan map distance from the main construction yard. MCV BFS
        // remains the independent reachability check and final travel tie-break.
        const int baseDistance=mainBase>=0 ? std::abs(x-mainBase%w)+std::abs(y-mainBase/w) : distance[i];
        const Site candidate{x,y,room,clearance,distance[i],baseDistance};
        if(betterSite(candidate,best))best=candidate;
    }
    return best;
}
}
#endif
