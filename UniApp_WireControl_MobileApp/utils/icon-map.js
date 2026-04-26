const ICON_TEMPLATES = {
  radio:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><path d='M12 3v2.6'/><path d='M6.8 6.8a7.3 7.3 0 0 0 0 10.4'/><path d='M17.2 6.8a7.3 7.3 0 0 1 0 10.4'/><path d='M9.5 9.5a3.6 3.6 0 0 0 0 5'/><path d='M14.5 9.5a3.6 3.6 0 0 1 0 5'/><circle cx='12' cy='12' r='1.4' fill='__COLOR__' stroke='none'/></svg>",
  wifi:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><path d='M5 8.8a11.3 11.3 0 0 1 14 0'/><path d='M8.4 12.2a6.4 6.4 0 0 1 7.2 0'/><path d='M11 15.6a2.2 2.2 0 0 1 2 0'/><circle cx='12' cy='18' r='1.2' fill='__COLOR__' stroke='none'/></svg>",
  bluetooth:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><path d='M12 3v18'/><path d='M7.5 7.2L16 12l-8.5 4.8V7.2z'/><path d='M7.5 7.2L12 3l4 4.1'/><path d='M7.5 16.8L12 21l4-4.1'/></svg>",
  "wifi-off":
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><path d='M5 8.8a11.3 11.3 0 0 1 11-1.1'/><path d='M8.4 12.2a6.4 6.4 0 0 1 5.2-.6'/><path d='M11 15.6a2.2 2.2 0 0 1 2 0'/><circle cx='12' cy='18' r='1.2' fill='__COLOR__' stroke='none'/><path d='M3 3l18 18'/></svg>",
  "bluetooth-off":
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><path d='M12 3v18'/><path d='M7.5 7.2L16 12l-8.5 4.8V7.2z'/><path d='M7.5 7.2L12 3l4 4.1'/><path d='M7.5 16.8L12 21l4-4.1'/><path d='M3 3l18 18'/></svg>",
  loader:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round'><path d='M12 3a9 9 0 1 0 9 9'/></svg>",
  signal:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none'><rect x='4' y='13' width='2.6' height='7' rx='1.1' fill='__COLOR__'/><rect x='8.6' y='10.5' width='2.6' height='9.5' rx='1.1' fill='__COLOR__' opacity='0.82'/><rect x='13.2' y='8' width='2.6' height='12' rx='1.1' fill='__COLOR__' opacity='0.64'/><rect x='17.8' y='5.5' width='2.6' height='14.5' rx='1.1' fill='__COLOR__' opacity='0.46'/></svg>",
  chevron:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><path d='M9 6l6 6-6 6'/></svg>",
  check:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><circle cx='12' cy='12' r='9'/><path d='M8.5 12.2l2.5 2.4 4.6-5.1'/></svg>",
  cpu:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><rect x='7' y='7' width='10' height='10' rx='1.6'/><path d='M9 3v3M15 3v3M9 18v3M15 18v3M3 9h3M3 15h3M18 9h3M18 15h3'/></svg>",
  power:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><path d='M12 3v7'/><path d='M7.5 5.8a8 8 0 1 0 9 0'/></svg>",
  shield:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><path d='M12 3l7 3v5.4c0 4.3-2.7 8.1-7 9.6-4.3-1.5-7-5.3-7-9.6V6l7-3z'/><path d='M12 8.4v4.7'/><circle cx='12' cy='16.4' r='1.1' fill='__COLOR__' stroke='none'/></svg>",
  lock:
    "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='__COLOR__' stroke-width='__STROKE__' stroke-linecap='round' stroke-linejoin='round'><rect x='5.4' y='11' width='13.2' height='9' rx='1.8'/><path d='M8.4 11V8.6a3.6 3.6 0 1 1 7.2 0V11'/></svg>"
};

export function buildIconDataUri(name, color, strokeWidth) {
  const template = ICON_TEMPLATES[name] || ICON_TEMPLATES.radio;
  const svg = template
    .replace(/__COLOR__/g, color || '#303133')
    .replace(/__STROKE__/g, String(strokeWidth || 1.9));

  return `data:image/svg+xml;utf8,${encodeURIComponent(svg)}`;
}
