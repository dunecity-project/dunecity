// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BREACHTRIGGER_H
#define BREACHTRIGGER_H

#include <Trigger/Trigger.h>

#include <string>

/**
    A scripted breach in a mountain wall (the [EVENTS] section's BreachN keys,
    docs/city-campaign.md): at its cycle every mountain tile in the rectangle becomes sand, the
    worm regions are rebuilt so sandworms can cross, `worms` new Natives sandworms surface in the
    gap and the news ticker reports it. Saved and loaded like every other trigger (type 3).
*/
class BreachTrigger : public Trigger {
public:
    BreachTrigger(Uint32 triggerCycleNumber, int x, int y, int width, int height, int worms, std::string message);
    explicit BreachTrigger(InputStream& stream);
    ~BreachTrigger() override = default;

    void save(OutputStream& stream) const override;
    void trigger() override;

    int getX() const { return x; }
    int getY() const { return y; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }

private:
    int x = 0, y = 0, width = 0, height = 0;
    int worms = 0;
    std::string message;
};

#endif // BREACHTRIGGER_H
