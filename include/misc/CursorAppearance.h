#ifndef CURSORAPPEARANCE_H
#define CURSORAPPEARANCE_H
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace CursorAppearance {
enum class Action { Pointer, Move, Attack, Capture, Drop, Heal, Repair, Return, Deploy, Destruct, Paths, Patrol };
/// Cursors that pick a map position keep their hotspot clear so the exact target stays visible.
inline bool aimsAtHotspot(Action action) {
    return (action >= Action::Move && action <= Action::Heal) || action == Action::Patrol;
}
inline SDL_Point hotspot(Action action, float scale) {
    const float factor = scale / 1.5f;
    const int coordinate = static_cast<int>(std::lround((action == Action::Pointer ? 6 : 16) * factor));
    return {coordinate, coordinate};
}
inline float segmentDistance(SDL_FPoint p, SDL_FPoint a, SDL_FPoint b) {
    const float dx=b.x-a.x, dy=b.y-a.y;
    const float t=std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/(dx*dx+dy*dy),0.0f,1.0f);
    return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}
inline float polygonDistance(SDL_FPoint p, std::initializer_list<SDL_FPoint> points) {
    bool inside=false;
    float distance=1000;
    auto a=*(points.end()-1);
    for(auto b:points) {
        if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
        distance=std::min(distance,segmentDistance(p,a,b));
        a=b;
    }
    return inside ? -distance : distance;
}
inline float shapeDistance(Action action, SDL_FPoint p) {
    switch(action) {
        case Action::Pointer:
            // A compact, slightly rounded arrow, with its tip at the hotspot.
            return polygonDistance(p,{{4,4},{23,13},{14,15},{10,25}})-0.3f;
        case Action::Move:
            return polygonDistance(p,{{14,3},{19,8},{16,8},{16,12},{20,12},{20,9},{25,14},{20,19},{20,16},{16,16},{16,20},{19,20},{14,25},{9,20},{12,20},{12,16},{8,16},{8,19},{3,14},{8,9},{8,12},{12,12},{12,8},{9,8}});
        case Action::Attack:
            return std::min({std::abs(std::hypot(p.x-14,p.y-14)-7)-1.2f,
                segmentDistance(p,{14,2},{14,8})-1.1f,segmentDistance(p,{14,20},{14,26})-1.1f,
                segmentDistance(p,{2,14},{8,14})-1.1f,segmentDistance(p,{20,14},{26,14})-1.1f,
                std::hypot(p.x-14,p.y-14)-1.5f});
        case Action::Capture:
            return std::min({segmentDistance(p,{7,4},{7,24})-1.3f,
                polygonDistance(p,{{8,5},{23,5},{19,10},{23,15},{8,15}}),
                segmentDistance(p,{4,24},{14,24})-1.1f});
        case Action::Drop:
            return std::min({polygonDistance(p,{{12,3},{16,3},{16,12},{22,12},{14,20},{6,12},{12,12}}),
                segmentDistance(p,{5,21},{5,25})-1,segmentDistance(p,{5,25},{23,25})-1,
                segmentDistance(p,{23,25},{23,21})-1});
        case Action::Repair:
            return std::min(segmentDistance(p,{7,23},{19,11})-2.3f,
                std::max(std::hypot(p.x-20,p.y-8)-5.5f,
                    -polygonDistance(p,{{17,1},{17,8},{21,11},{28,5},{29,0}})));
        case Action::Return:
            return std::min(segmentDistance(p,{23,9},{23,22})-1.5f,
                std::min(segmentDistance(p,{9,22},{23,22})-1.5f,
                    polygonDistance(p,{{4,9},{11,3},{11,7},{23,7},{23,11},{11,11},{11,15}})));
        case Action::Deploy:
            return std::min(polygonDistance(p,{{4,13},{14,4},{24,13},{21,13},{21,24},{7,24},{7,13}}),
                segmentDistance(p,{3,25},{25,25})-1);
        case Action::Destruct:
            return polygonDistance(p,{{14,2},{17,9},{24,5},{21,12},{27,15},{20,18},{23,25},{16,22},{11,27},{9,20},{2,22},{7,15},{2,9},{10,10}});
        case Action::Paths:
            return std::min({segmentDistance(p,{5,23},{11,12})-1.3f,
                segmentDistance(p,{11,12},{23,5})-1.3f,
                std::abs(std::hypot(p.x-5,p.y-23)-2.5f)-1,
                std::abs(std::hypot(p.x-23,p.y-5)-2.5f)-1});
        case Action::Heal:
            return polygonDistance(p,{{11,4},{17,4},{17,11},{24,11},{24,17},{17,17},{17,24},{11,24},{11,17},{4,17},{4,11},{11,11}})-0.2f;
        case Action::Patrol:
            // Two opposed arrows: out to the point and back again.
            return std::min({segmentDistance(p,{4,9},{20,9})-1.3f,
                polygonDistance(p,{{19,4},{26,9},{19,14}}),
                segmentDistance(p,{8,19},{24,19})-1.3f,
                polygonDistance(p,{{9,14},{2,19},{9,24}})});
    }
    return 1000;
}
// The same vector geometry creates platform cursors for SDL desktop and web.
// Supersampling at the chosen size keeps the outline smooth at fractional scales.
inline SDL_Surface* create(Action action, float scale, bool targeting = true) {
    if(!std::isfinite(scale) || scale<1 || scale>4) return nullptr;
    const float factor=scale/1.5f;
    const int extent=static_cast<int>(std::ceil(33*factor));
    auto* surface=SDL_CreateRGBSurfaceWithFormat(0,extent,extent,32,SDL_PIXELFORMAT_RGBA32);
    if(!surface) return nullptr;
    constexpr int samples=4;
    for(int y=0;y<extent;++y) for(int x=0;x<extent;++x) {
        int coverage=0, brightness=0;
        for(int sy=0;sy<samples;++sy) for(int sx=0;sx<samples;++sx) {
            const SDL_FPoint p{(x+(sx+0.5f)/samples)/factor-2,(y+(sy+0.5f)/samples)/factor-2};
            const float distance=shapeDistance(action,p)*factor;
            const bool pointer = action == Action::Pointer;
            // Action symbols contain no white paint. Leave a clear opening at
            // the click hotspot as well, so the exact target stays visible.
            const bool aiming = targeting && aimsAtHotspot(action);
            if(aiming && std::hypot(p.x-14,p.y-14)<3.5f) continue;
            if(distance <= (pointer ? 1.75f : 0.0f)) {
                ++coverage;
                brightness += pointer && distance>0 ? 255 : 16;
            }
        }
        const Uint8 color=coverage ? brightness/coverage : 0;
        const Uint8 alpha=coverage*255/(samples*samples);
        auto* row=reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels)+y*surface->pitch);
        row[x]=SDL_MapRGBA(surface->format,color,color,color,alpha);
    }
    return surface;
}
// A compact version of the same geometry fits the existing 26px sidebar rows.
inline SDL_Surface* createIcon(Action action) {
    SDL_Surface* source = create(action,1.0f);
    if(!source) return nullptr;
    SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormat(0,20,20,32,SDL_PIXELFORMAT_RGBA32);
    if(icon) {
        const SDL_Rect crop{1,1,20,20};
        SDL_SetSurfaceBlendMode(source,SDL_BLENDMODE_NONE);
        SDL_BlitSurface(source,&crop,icon,nullptr);
        for(int y=0;y<icon->h;++y) for(int x=0;x<icon->w;++x) {
            auto* row=reinterpret_cast<Uint32*>(static_cast<Uint8*>(icon->pixels)+y*icon->pitch);
            Uint8 red,green,blue,alpha;SDL_GetRGBA(row[x],icon->format,&red,&green,&blue,&alpha);
            row[x]=SDL_MapRGBA(icon->format,250,248,240,alpha);
        }
        SDL_SetSurfaceBlendMode(icon,SDL_BLENDMODE_BLEND);
    }
    SDL_FreeSurface(source);
    return icon;
}
}
#endif
