const lg = (id, a, b) => `<linearGradient id="${id}" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="${a}"/><stop offset="1" stop-color="${b}"/></linearGradient>`;
const note = (x, y, s, f = '#fff') => `<g transform="translate(${x},${y}) scale(${s})" fill="${f}"><path d="M30 8 L78 0 L78 62 A16 14 0 1 1 66 49 L66 22 L42 26 L42 70 A16 14 0 1 1 30 57 Z"/></g>`;
const play = (cx, cy, r, f = '#fff') => `<path d="M${cx - r * 0.45} ${cy - r * 0.6} L${cx - r * 0.45} ${cy + r * 0.6} L${cx + r * 0.6} ${cy} Z" fill="${f}" stroke="${f}" stroke-width="${r * 0.18}" stroke-linejoin="round"/>`;
const padS = (cx, cy, s, f = '#fff') => `<g transform="translate(${cx},${cy}) scale(${s})" fill="${f}"><rect x="-70" y="-11" width="56" height="22" rx="11"/><rect x="-53" y="-28" width="22" height="56" rx="11"/>
  <circle cx="34" cy="-16" r="13"/><circle cx="58" cy="8" r="13"/><circle cx="10" cy="8" r="13" opacity="0.7"/><circle cx="34" cy="32" r="13" opacity="0.7"/></g>`;
const waves = (cx, cy, s, f = '#fff') => `<g transform="translate(${cx},${cy}) scale(${s})"><g fill="none" stroke="${f}" stroke-width="16" stroke-linecap="round"><path d="M-36 -42 A60 60 0 0 0 -36 42"/><path d="M36 -42 A60 60 0 0 1 36 42"/></g><circle r="16" fill="${f}"/></g>`;
const disc = (cx, cy, R, hole, inner) => {
  let g = ''; for (let i = 0; i < 6; i++) { const r = hole + 14 + (R - hole - 22) * i / 5; g += `<circle cx="${cx}" cy="${cy}" r="${r}" fill="none" stroke="#fff" stroke-opacity="0.16" stroke-width="2"/>`; }
  return `<circle cx="${cx}" cy="${cy}" r="${R}" fill="url(#dk)"/>${g}<circle cx="${cx}" cy="${cy}" r="${R}" fill="url(#hl)"/><circle cx="${cx}" cy="${cy}" r="${hole}" fill="#1B1340"/>${inner}`;
};
const dkdefs = `<radialGradient id="dk" cx="0.35" cy="0.3" r="0.9"><stop offset="0" stop-color="#E2D8FF"/><stop offset="0.45" stop-color="#9A7BFF"/><stop offset="1" stop-color="#4B2BC4"/></radialGradient>
  <linearGradient id="hl" x1="0" y1="0" x2="1" y2="1"><stop offset="0.3" stop-color="#fff" stop-opacity="0"/><stop offset="0.5" stop-color="#fff" stop-opacity="0.35"/><stop offset="0.7" stop-color="#fff" stop-opacity="0"/></linearGradient>`;
const eq = (cx, cy, s, f) => `<g transform="translate(${cx},${cy}) scale(${s})" fill="${f}"><rect x="-46" y="-20" width="16" height="40" rx="8"/><rect x="-20" y="-42" width="16" height="84" rx="8"/><rect x="6" y="-30" width="16" height="60" rx="8"/><rect x="32" y="-12" width="16" height="24" rx="8"/></g>`;
module.exports = {
  // A : quatre tuiles, la 1re est le disque (musique) ; lecture, jeux, radio
  'melange-a': () => `<defs>${dkdefs}${lg('t0', '#2A1A6E', '#1B1340')}${lg('t1', '#8D72FF', '#6A48F0')}${lg('t2', '#5B3BE0', '#4325BE')}${lg('t3', '#3A1F9E', '#2A1478')}</defs>
    <rect x="40" y="40" width="200" height="200" rx="52" fill="url(#t0)"/>${disc(140, 140, 84, 30, eq(140, 140, 0.42, '#FFD36B'))}
    <rect x="272" y="40" width="200" height="200" rx="52" fill="url(#t1)"/>${play(372, 140, 70)}
    <rect x="40" y="272" width="200" height="200" rx="52" fill="url(#t2)"/>${padS(140, 372, 1.05, '#FFD36B')}
    <rect x="272" y="272" width="200" height="200" rx="52" fill="url(#t3)"/>${waves(372, 372, 1)}`,
  // B : le disque au centre, pose sur quatre tuiles (une icone dans chaque coin)
  'melange-b': () => `<defs>${dkdefs}${lg('t1', '#8D72FF', '#6A48F0')}${lg('t2', '#5B3BE0', '#4325BE')}${lg('t3', '#4A2CC9', '#3A1F9E')}${lg('t4', '#3A1F9E', '#2A1478')}</defs>
    <rect x="24" y="24" width="224" height="224" rx="56" fill="url(#t1)"/><rect x="264" y="24" width="224" height="224" rx="56" fill="url(#t2)"/>
    <rect x="24" y="264" width="224" height="224" rx="56" fill="url(#t3)"/><rect x="264" y="264" width="224" height="224" rx="56" fill="url(#t4)"/>
    ${note(58, 58, 0.95)}${play(420, 92, 44)}${padS(98, 422, 0.75, '#FFD36B')}${waves(420, 420, 0.7)}
    <circle cx="256" cy="256" r="150" fill="#120A2E"/>${disc(256, 256, 138, 48, eq(256, 256, 0.62, '#FFD36B'))}`,
  // C : le disque dont l'etiquette est faite de quatre tuiles de couleur
  'melange-c': () => `<defs>${dkdefs}${lg('bg', '#1B1340', '#3A1F8F')}</defs>
    <rect x="16" y="16" width="480" height="480" rx="120" fill="url(#bg)"/>
    ${disc(256, 256, 196, 0, '')}
    <g><rect x="140" y="140" width="112" height="112" rx="30" fill="#7B5CFF"/><rect x="260" y="140" width="112" height="112" rx="30" fill="#FF6FB5"/>
       <rect x="140" y="260" width="112" height="112" rx="30" fill="#41D3C4"/><rect x="260" y="260" width="112" height="112" rx="30" fill="#FFC25C"/></g>
    ${note(161, 158, 0.82)}${play(318, 196, 40)}${padS(198, 316, 0.66)}${waves(316, 316, 0.56)}`,
};
