#pragma once

// Fills an empty shot history with a few made-up shots, so the Last shot side
// panel and the web history have something to show on a fresh simulator.
// Must run after LittleFS is mounted and before ShotHistoryPlugin starts.
void seedDemoShots();
