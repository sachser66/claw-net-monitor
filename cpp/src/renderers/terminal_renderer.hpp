#pragma once

#include <string>
#include <vector>

#include "../core/snapshot.hpp"
#include "../core/types.hpp"

std::string fmt_rate(double value);
std::string shorten(const std::string& text, int width);
std::string summarize_group(const GroupStat& g);
std::string make_real_flow_line(const std::string& from, const std::string& to, int tick, int width, double activity, const std::string& evidence);
int color_for_status(const std::string& status);
int color_for_kind(const std::string& kind);
int color_for_channel(const std::string& channel);
void box(int y, int x, int h, int w, const std::string& title, int color_pair);
void render_terminal(const Snapshot& snapshot, const std::vector<GroupStat>& groups, int tick);
