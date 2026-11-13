// Aquarium: what the controllers show and the CC each control sends (channel 1).
// src/app.cpp binds the same numbers in kBind.
export const profile = {
  name: 'Aquarium',
  accent: '#1f8fa6',
  pages: [
    {key: 'P', label: 'PLAY', title: 'Steer the sub', color: '#3FB8C9', controls: [
      {id: 'dive', cc: 30, kind: 'trigger', label: 'DIVE', key: 'A'},
      {id: 'rise', cc: 31, kind: 'trigger', label: 'RISE', key: 'S'},
      {id: 'ping', cc: 32, kind: 'trigger', label: 'PING', key: 'D'},
      {id: 'storm', cc: 33, kind: 'trigger', label: 'STORM', key: 'F'},
      {id: 'water', kind: 'xy', label: 'Depth × Current', x: 'depth', y: 'current'},
    ]},
    {key: 'S', label: 'SEA', title: 'The sea', color: '#2E7DA8', controls: [
      {id: 'depth', cc: 20, kind: 'range', label: 'Depth', min: 0, max: 100, def: 40, format: v => `${v * 2} m`},
      {id: 'current', cc: 21, kind: 'range', label: 'Current', min: 0, max: 100, unit: '%', def: 35},
      {id: 'bubbles', cc: 22, kind: 'choice', label: 'Bubbles', options: ['Few', 'Some', 'Lots'], def: 1},
    ]},
    {key: 'M', label: 'MIX', title: 'Sonar and mix', color: '#E3B448', controls: [
      {id: 'sonar', cc: 23, kind: 'toggle', label: 'Sonar', def: 0},
      {id: 'sonarAmt', cc: 24, kind: 'range', label: 'Sonar echo', min: 0, max: 100, unit: '%', def: 75},
      {id: 'mix', cc: 25, kind: 'range', label: 'Mix', min: 0, max: 100, unit: '%', def: 80},
      {id: 'level', cc: 26, kind: 'range', label: 'Level', min: -12, max: 6, unit: 'dB', def: 0},
    ]},
  ],
};
