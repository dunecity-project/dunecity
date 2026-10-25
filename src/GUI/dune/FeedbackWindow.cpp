#include <GUI/dune/FeedbackWindow.h>
#include <Game.h>
#include <House.h>
#include <globals.h>
#include <sand.h>
#include <config.h>
#include <misc/FeedbackIssue.h>
#include <misc/string_util.h>
#include <FileClasses/TextManager.h>
#include <algorithm>
#include <players/HumanPlayer.h>
#include <players/QuantBot.h>
#include <players/PlayerFactory.h>
#include <random>

void FeedbackEditor::refresh() {
    setTextColor(selectAll ? COLOR_BLACK : COLOR_WHITE, COLOR_TRANSPARENT, selectAll ? COLOR_WHITE : COLOR_TRANSPARENT);
    TextView::setText(contents.substr(0, cursor) + (isActive() ? "|" : "") + contents.substr(cursor));
    if(cursor == contents.size()) scrollToEnd();
}
void FeedbackEditor::setActive() { TextView::setActive(); refresh(); }
void FeedbackEditor::setInactive() { TextView::setInactive(); selectAll = false; refresh(); }
void FeedbackEditor::setActive(bool active) {
    TextView::setActive(active);
    if(!active) selectAll = false;
    refresh();
}
bool FeedbackEditor::handleMouseLeft(Sint32 x, Sint32 y, bool pressed) {
    if(!isEnabled() || !isVisible()) return false;
    if(x < 0 || y < 0 || x >= getSize().x || y >= getSize().y) return false;
    if(pressed) setActive();
    TextView::handleMouseLeft(x, y, pressed);
    return true;
}
void FeedbackEditor::insert(const std::string& text) {
    std::string clean;
    for(unsigned char c : text) if(c >= 32 || c == '\n' || c == '\t') clean += static_cast<char>(c);
    if((selectAll ? 0 : utf8Length(contents)) + utf8Length(clean) > 2000) return;
    if(selectAll) { contents.clear(); cursor = 0; selectAll = false; }
    contents.insert(cursor, clean); cursor += clean.size(); refresh();
}
bool FeedbackEditor::handleTextInput(SDL_TextInputEvent& event) {
    if(!isActive() || !isEnabled()) return false;
    insert(event.text); return true;
}
bool FeedbackEditor::handleKeyPress(SDL_KeyboardEvent& key) {
    if(!isActive() || !isEnabled()) return false;
    const auto previous = [this] {
        size_t pos = cursor;
        if(pos) { --pos; while(pos && (static_cast<unsigned char>(contents[pos]) & 0xc0) == 0x80) --pos; }
        return pos;
    };
    const auto next = [this] {
        size_t pos = cursor;
        if(pos < contents.size()) { ++pos; while(pos < contents.size() && (static_cast<unsigned char>(contents[pos]) & 0xc0) == 0x80) ++pos; }
        return pos;
    };
    if(key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) {
        if(key.keysym.sym == SDLK_a) { selectAll = true; refresh(); return true; }
        if(key.keysym.sym == SDLK_v) {
            char* text = SDL_GetClipboardText();
            if(text) { insert(text); SDL_free(text); }
            return true;
        }
        if((key.keysym.sym == SDLK_c || key.keysym.sym == SDLK_x) && selectAll) {
            SDL_SetClipboardText(contents.c_str());
            if(key.keysym.sym == SDLK_x) { contents.clear(); cursor = 0; selectAll = false; refresh(); }
            return true;
        }
    }
    switch(key.keysym.sym) {
        case SDLK_RETURN: insert("\n"); return true;
        case SDLK_TAB: setInactive(); return true;
        case SDLK_BACKSPACE:
        case SDLK_DELETE:
            if(selectAll) { contents.clear(); cursor = 0; }
            else if(key.keysym.sym == SDLK_BACKSPACE) { const auto pos = previous(); contents.erase(pos, cursor - pos); cursor = pos; }
            else contents.erase(cursor, next() - cursor);
            break;
        case SDLK_LEFT: cursor = previous(); break;
        case SDLK_RIGHT: cursor = next(); break;
        case SDLK_HOME: cursor = 0; break;
        case SDLK_END: cursor = contents.size(); break;
        default: return TextView::handleKeyPress(key);
    }
    selectAll = false; refresh(); return true;
}

FeedbackWindow::FeedbackWindow(Submit send) : Window(0, 0, 560, 440), send(std::move(send)) {
    setCurrentPosition(std::max(0, (getRendererWidth() - 560) / 2),
                       std::max(0, (getRendererHeight() - 440) / 2), 560, 440);
    setWindowWidget(&layout);
    auto label = [this](Label& item, const char* text, int y, int height = 20) {
        item.setText(*text ? _(text) : std::string()); item.setTextColor(COLOR_WHITE); item.setTextFontSize(12);
        layout.addWidget(&item, Point(16, y), Point(528, height));
    };
    label(heading, "Give feedback", 12, 24);
    heading.setTextFontSize(16);
    label(helpLabel, "Creates a public request. No GitHub account needed.", 40);
    label(summaryLabel, "Summary", 67);
    summary.setMaximumTextLength(100);
    layout.addWidget(&summary, Point(16, 88), Point(528, 24));
    label(detailsLabel, "Feedback (up to 2,000 characters; paste supported)", 122);
    details.setTextColor(COLOR_WHITE, COLOR_BLACK);
    layout.addWidget(&details, Point(16, 145), Point(528, 150));

    const auto& init = currentGame->getGameInitSettings();
    context = "DuneCity " + std::string(VERSION) + "\nPlatform: " + SDL_GetPlatform()
        + "\nMod: " + init.getModName()
        + "\nHouse: " + getHouseNameByNumber(static_cast<HOUSETYPE>(pLocalHouse->getHouseID()));
    if(init.getMission() > 0) context += "\nLevel: " + std::to_string(missionNumberToLevelNumber(init.getMission()))
        + " / Scenario: " + std::to_string(init.getMission());
    context += "\nAI players:";
    bool hasAI = false;
    // Player houses only; the Natives house has no players and is not part of the match setup.
    for(int houseId = 0; houseId < NUM_PLAYER_HOUSES; ++houseId) {
        const auto* house = currentGame->getHouse(houseId);
        if(!house) continue;
        for(const auto& player : house->getPlayerList()) {
            if(dynamic_cast<const HumanPlayer*>(player.get())) continue;
            const auto* type = PlayerFactory::getByPlayerClass(player->getPlayerclass());
            context += "\n" + getHouseNameByNumber(static_cast<HOUSETYPE>(houseId)) + ": "
                + (type ? type->getName() : player->getPlayerclass());
            if(const auto* bot = dynamic_cast<const QuantBot*>(player.get()))
                context += " (difficulty: " + bot->getDifficultyName() + ")";
            else if(player->getPlayerclass() == "CampaignAIPlayer") context += " (scenario settings)";
            else if(player->getPlayerclass() == "SmartBot") context += " (difficulty: Normal)";
            hasAI = true;
        }
    }
    if(!hasAI) context += " None";
    contextView.setTextFontSize(11);
    contextView.setTextColor(COLOR_WHITE);
    contextView.setText("Included with your feedback:\n" + context);
    layout.addWidget(&contextView, Point(16, 303), Point(528, 65));
    label(statusLabel, "", 372);
    openButton.setText(_("Send feedback"));
    openButton.setOnClick([this] { sendFeedback(); });
    layout.addWidget(&openButton, Point(16, 401), Point(250, 26));
    closeButton.setText(_("Back to game"));
    closeButton.setOnClick([] { currentGame->resumeGame(); });
    layout.addWidget(&closeButton, Point(294, 401), Point(250, 26));
    textInputWasActive = SDL_IsTextInputActive();
    SDL_StartTextInput();
    summary.setActive();
}
FeedbackWindow::~FeedbackWindow() { if(!textInputWasActive) SDL_StopTextInput(); }
bool FeedbackWindow::handleKeyPress(SDL_KeyboardEvent& key) {
    if(key.keysym.sym == SDLK_ESCAPE && !submission) { currentGame->resumeGame(); return true; }
    return Window::handleKeyPress(key);
}
void FeedbackWindow::draw(Point position) {
    pollSubmission();
    Window::draw(position);
}
void FeedbackWindow::pollSubmission() {
    if(!submission || !submission->done.load()) return;
    const auto response = submission->response;
    submission.reset();
    openButton.setEnabled(true);
    closeButton.setEnabled(true);
    summary.setEnabled(true); details.setEnabled(true);
    if(response.compare(0, 3, "OK ") == 0 && FeedbackIssue::isIssueUrl(response.substr(3))) {
        issueUrl = response.substr(3);
        helpLabel.setText(_("Thank you! Your request has been created."));
        statusLabel.setText(_("You can view the request or click OK to return to the game."));
        summary.setEnabled(false); details.setEnabled(false);
        openButton.setText(_("View request"));
        closeButton.setText(_("OK"));
    } else {
        statusLabel.setText(response.compare(0, 6, "ERROR ") == 0 && response.size() < 200
            ? response.substr(6) : _("Could not send feedback. Your text is still here."));
        openButton.setText(_("Try again"));
    }
}
void FeedbackWindow::sendFeedback() {
    if(!issueUrl.empty()) {
        if(SDL_OpenURL(issueUrl.c_str()) != 0)
            statusLabel.setText(_("Could not open the browser. Your request was still created."));
        return;
    }
    if(submission) return;
    try {
        // Keep one id across retries of the same text, including a lost success response.
        if(requestId.empty() || submittedTitle != summary.getText() || submittedDetails != details.value()) {
            std::random_device random;
            constexpr char hex[] = "0123456789abcdef";
            requestId.clear();
            for(int i = 0; i < 32; ++i) requestId += hex[random() & 15];
        }
        auto fields = FeedbackIssue::fields(requestId, summary.getText(), details.value(), context);
        submittedTitle = summary.getText(); submittedDetails = details.value();
        submission = send(fields);
        openButton.setEnabled(false);
        closeButton.setEnabled(false);
        summary.setEnabled(false); details.setEnabled(false);
        openButton.setText(_("Sending..."));
        statusLabel.setText(_("Sending feedback. This can take up to 20 seconds."));
    } catch(const std::exception& error) {
        statusLabel.setText(error.what());
    }
}
