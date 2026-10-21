#include <mod/WorkshopClient.h>
#include <Menu/MenuBase.h>
#include <FileClasses/GFXManager.h>
#include <GUI/StaticContainer.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <globals.h>
#include <algorithm>

namespace Workshop {
namespace {
// Modal progress for the exact revision an online game needs: the host's authored mod
// before advertising, or the pinned mod a joiner or loaded save is missing.
class ProgressMenu final : public MenuBase {
    StaticContainer layout;
    Label title, detail;
    TextButton close;
    Client client;
    bool finished = false, success = false;
public:
    ProgressMenu(const Revision* revision, const std::string& hash) {
        setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
        resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
        setWindowWidget(&layout);
        const int width = std::min(620, getSize().x-32);
        const int x = (getSize().x-width)/2, y = (getSize().y-180)/2;
        title.setText(revision ? "Sharing the game's mod" : "Downloading the game's mod");
        title.setAlignment(Alignment_HCenter); title.setTextFontSize(22);
        detail.setTextFontSize(13); detail.setAlignment(Alignment_HCenter);
        layout.addWidget(&title, Point(x,y), Point(width,40));
        layout.addWidget(&detail, Point(x,y+50), Point(width,80));
        close.setText("Cancel");
        close.setOnClick([this]() { client.cancel(); quit(); });
        layout.addWidget(&close, Point(x+width/2-80,y+140), Point(160,32));
        close.setActive();
        if(revision) client.publish(*revision);
        else client.download(hash);
        detail.setText(client.message());
    }
    void update() override {
        if(finished) return;
        client.update(); detail.setText(client.message());
        if(client.status() == Client::Status::Succeeded) { success = true; finished = true; quit(); }
        else if(client.status() == Client::Status::Failed) { finished = true; close.setText("Back"); }
    }
    bool run() { showMenu(); return success; }
};
}

bool publishWithProgress(const Revision& revision) { return ProgressMenu(&revision,"").run(); }
bool downloadWithProgress(const std::string& hash) { return ProgressMenu(nullptr,hash).run(); }
}
