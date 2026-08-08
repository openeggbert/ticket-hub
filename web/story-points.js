// Fixed estimation scale is deliberately a standalone, dependency-free UI
// module. Keeping it outside app.js lets the create form, ticket drawer and
// future alternative views share exactly one definition without a framework
// or a build step.
export const STORY_POINT_OPTIONS = [
  { value: 0, label: '0 (no effort)' },
  { value: 0.25, label: '0.25 (quarter a day)' },
  { value: 0.5, label: '0.5 (half a day)' },
  { value: 1, label: '1 (about one day)' },
  { value: 2, label: '2 (about two days)' },
  { value: 3, label: '3 (about three days)' },
  { value: 5, label: '5 (about one week)' },
  { value: 8, label: '8 (about two weeks)' },
  { value: 13, label: '13 (about three weeks)' },
  { value: 20, label: '20 (about one month)' },
  { value: 40, label: '40 (about two months)' },
  { value: 100, label: '100 (split the ticket)' }
];

export function storyPointsOptionsMarkup(selectedValue, escapeHtml) {
  const selected = selectedValue === null || selectedValue === undefined ? '' : String(selectedValue);
  const isStandardValue = STORY_POINT_OPTIONS.some(option => String(option.value) === selected);
  const legacyOption = selected && !isStandardValue
    ? `<option value="${escapeHtml(selected)}" selected>${escapeHtml(selected)} (existing value)</option>`
    : '';
  return `<option value="" ${selected ? '' : 'selected'}>Not estimated</option>${legacyOption}${STORY_POINT_OPTIONS.map(option =>
    `<option value="${option.value}" ${String(option.value) === selected ? 'selected' : ''}>${option.label}</option>`).join('')}`;
}
