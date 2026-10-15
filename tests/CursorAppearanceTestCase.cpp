#include <catch2/catch_test_macros.hpp>
#include <misc/CursorAppearance.h>
#include <misc/SDL2pp.h>
#include <set>
#include <vector>

TEST_CASE("All cursor actions retain contrast and smooth edges at every supported size", "[rendering][cursor]") {
    for(float scale : {1.0f,1.5f,2.0f,3.0f,4.0f}) {
        std::set<std::vector<Uint32>> silhouettes;
        for(int mode=0;mode<=static_cast<int>(CursorAppearance::Action::Patrol);++mode) {
            CAPTURE(scale,mode);
            const auto action=static_cast<CursorAppearance::Action>(mode);
            sdl2::surface_ptr surface{CursorAppearance::create(action,scale)};
            REQUIRE(surface);
            const auto hotspot=CursorAppearance::hotspot(action,scale);
            CHECK(hotspot.x>=0);CHECK(hotspot.y>=0);
            CHECK(hotspot.x<surface->w);CHECK(hotspot.y<surface->h);
            CHECK(surface->w<=128);CHECK(surface->h<=128);
            int black=0, white=0, smooth=0, transparent=0;
            std::vector<Uint32> pixels;
            for(int y=0;y<surface->h;++y)for(int x=0;x<surface->w;++x) {
                const auto pixel=reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels)+y*surface->pitch)[x];
                pixels.push_back(pixel);
                Uint8 r,g,b,a;SDL_GetRGBA(pixel,surface->format,&r,&g,&b,&a);
                if(a==255 && r<32)++black;
                if(a==255 && r>240)++white;
                if(a>0 && a<255)++smooth;
                if(a==0)++transparent;
            }
            CHECK(black>0);CHECK(smooth>0);CHECK(transparent>0);
            if(action==CursorAppearance::Action::Pointer) CHECK(white>0);
            else CHECK(white==0);
            if(CursorAppearance::aimsAtHotspot(action)) {
                const int radius=static_cast<int>(scale/1.5f);
                for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
                    Uint8 r,g,b,a;
                    const auto pixel=reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels)+(hotspot.y+dy)*surface->pitch)[hotspot.x+dx];
                    SDL_GetRGBA(pixel,surface->format,&r,&g,&b,&a);
                    CHECK(a==0);
                }
            }
            CHECK(silhouettes.insert(pixels).second);
        }
    }
    CHECK(CursorAppearance::create(CursorAppearance::Action::Pointer,0)==nullptr);
}
