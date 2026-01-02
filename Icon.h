#pragma once
#include <string>

const std::string APP_ICON_SVG = R"svg(
<svg width="128" height="128" viewBox="0 0 128 128" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="grad1" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#4285f4;stop-opacity:1" />
      <stop offset="100%" style="stop-color:#34a853;stop-opacity:1" />
    </linearGradient>
  </defs>
  <circle cx="64" cy="64" r="62" fill="url(#grad1)" />
  <path d="M40 85c-8.8 0-16-7.2-16-16s7.2-16 16-16c.3 0 .7 0 1 .1C44.4 44.1 53.4 37 64 37c12.2 0 22.5 8.2 25.4 19.4 1.2-.4 2.5-.6 3.8-.6 6.1 0 11.2 4.4 12.5 10.2 6.5.6 11.6 6 11.6 12.6 0 7.2-5.8 13-13 13H40z" fill="white" />
  <rect x="52" y="58" width="24" height="18" rx="2" fill="#4285f4" />
  <text x="56" y="72" font-family="monospace" font-weight="bold" font-size="14" fill="white">&gt;_</text>
  <path d="M64 85l-5 10h10z" fill="white" />
</svg>
)svg";
