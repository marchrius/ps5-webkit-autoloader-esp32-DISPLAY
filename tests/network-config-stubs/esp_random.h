#pragma once
inline unsigned esp_random() { static unsigned next=1; return next++; }
