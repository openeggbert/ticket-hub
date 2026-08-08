import { test, expect } from '@playwright/test';
import AxeBuilder from '@axe-core/playwright';

async function signIn(page) {
  await page.goto('/');
  await page.locator('#login-form [name="email"]').fill('demo@ticket-hub.local');
  await page.locator('#login-form [name="password"]').fill('demo12345');
  await page.locator('#login-form').getByRole('button', { name: 'Sign in' }).click();
  await expect(page.getByRole('heading', { name: 'Dashboard' })).toBeVisible();
}

test('anonymous ticket API access is rejected', async ({ request }) => {
  const response = await request.get('/api/v1/tickets');
  expect(response.status()).toBe(401);
});

test('ticket creation, estimation, drawer, attachment and history stay usable', async ({ page }) => {
  await signIn(page);
  await page.locator('#create-button').click();
  const form = page.locator('#create-form');
  await expect(form.locator('[name="storyPoints"] option[value="0.25"]')).toHaveText('0.25 (quarter a day)');
  await form.locator('[name="projectKey"]').selectOption({ index: 0 });
  await form.locator('[name="summary"]').fill('Browser regression ticket');
  await form.locator('[name="storyPoints"]').selectOption('0.25');
  await form.getByRole('button', { name: 'Create ticket' }).click();

  const drawer = page.locator('#ticket-drawer');
  await expect(drawer).toBeVisible();
  await expect(drawer).toContainText('0.25');
  await expect(drawer.getByRole('button', { name: /History/ })).toBeVisible();

  const resizeHandle = drawer.locator('#drawer-resize-handle');
  await expect(resizeHandle).toBeVisible();
  await resizeHandle.focus();
  await resizeHandle.press('ArrowLeft');

  await drawer.locator('#attachment-file-input').setInputFiles({
    name: 'proof.txt',
    mimeType: 'text/plain',
    buffer: Buffer.from('Ticket Hub browser regression proof')
  });
  await expect(drawer.getByText('proof.txt')).toBeVisible();

  const accessibility = await new AxeBuilder({ page }).include('#ticket-drawer').analyze();
  expect(accessibility.violations).toEqual([]);
});

test('dashboard baseline has no automated accessibility violations', async ({ page }) => {
  await signIn(page);
  const accessibility = await new AxeBuilder({ page }).include('#content').analyze();
  expect(accessibility.violations).toEqual([]);
});
