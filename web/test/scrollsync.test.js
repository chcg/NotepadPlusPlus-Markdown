import { describe, expect, it } from 'vitest';

import { lineForTop, topForLine } from '../src/scrollsync.js';

// A document like:
//   0  # Title                 h1      top 0,   height 40
//   1  (blank)
//   2  para line               p       top 60,  height 40   (lines 2-3)
//   3  para line
//   4  (blank)
//   5  ```                     pre     top 120, height 200  (lines 5-14)
//   ...
//   14 ```
//   15 (blank)
//   16 - item one              ul      top 340, height 60   (lines 16-18)
//   17 - item two                li    top 340 / 370, height 30
const ENTRIES = [
  { line: 0, endLine: 1, top: 0, height: 40 },
  { line: 2, endLine: 4, top: 60, height: 40 },
  { line: 5, endLine: 15, top: 120, height: 200 },
  { line: 16, endLine: 18, top: 340, height: 60 },
  { line: 16, endLine: 17, top: 340, height: 30 },
  { line: 17, endLine: 18, top: 370, height: 30 },
];

describe('topForLine', () => {
  it('returns 0 for an empty document', () => {
    expect(topForLine([], 10)).toBe(0);
  });

  it('maps block start lines to block tops', () => {
    expect(topForLine(ENTRIES, 0)).toBe(0);
    expect(topForLine(ENTRIES, 2)).toBe(60);
    expect(topForLine(ENTRIES, 5)).toBe(120);
  });

  it('interpolates inside tall blocks such as fenced code (NppMarkdownPanel #127)', () => {
    // Line 10 is halfway through the 10-line code block.
    expect(topForLine(ENTRIES, 10)).toBe(220);
    expect(topForLine(ENTRIES, 14)).toBe(300);
  });

  it('prefers the innermost block (list item over list)', () => {
    expect(topForLine(ENTRIES, 17)).toBe(370);
  });

  it('interpolates across blank-line gaps between blocks', () => {
    // Line 1 is the gap between h1 (ends at 40) and p (starts at 60).
    expect(topForLine(ENTRIES, 1)).toBe(40);
    expect(topForLine(ENTRIES, 1.5)).toBe(50);
    expect(topForLine(ENTRIES, 15)).toBe(320);
  });

  it('clamps past the end of the document to the last block bottom', () => {
    expect(topForLine(ENTRIES, 500)).toBe(400);
  });
});

describe('lineForTop', () => {
  it('maps block tops back to their start lines', () => {
    expect(lineForTop(ENTRIES, 0)).toBe(0);
    expect(lineForTop(ENTRIES, 60)).toBe(2);
    expect(lineForTop(ENTRIES, 120)).toBe(5);
  });

  it('is the inverse of topForLine inside blocks', () => {
    for (const line of [0, 2, 3, 5, 7.5, 10, 14, 16, 17]) {
      expect(lineForTop(ENTRIES, topForLine(ENTRIES, line))).toBeCloseTo(line, 6);
    }
  });

  it('interpolates in gaps between blocks', () => {
    expect(lineForTop(ENTRIES, 50)).toBe(1.5);
  });

  it('handles positions before the first and after the last block', () => {
    const shifted = ENTRIES.map((e) => ({ ...e, top: e.top + 100 }));
    expect(lineForTop(shifted, 10)).toBe(0);
    expect(lineForTop(ENTRIES, 10_000)).toBe(18);
  });
});
