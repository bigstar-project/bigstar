import { describe, expect, test } from 'vitest';
import { render } from 'vitest-browser-react';
import { Tabs } from '../components/ui';
import { AIReplayViewer } from './AIReplayViewer';

const playlogLine = JSON.stringify({
  frame: 900,
  hash: '0x12345678',
  inputs: {
    player0: { held: 0x810 },
    player1: { held: 0x20 },
  },
  players: [
    {
      found: 1,
      pos: { x: 409600, y: 819200, z: 0 },
      powerup: 0,
      dead: 0,
      battleStars: 0,
      coins: 0,
      tileProbe: {
        summary: { effectiveHoleAhead: 0, wallAhead: 0 },
        samples: [],
      },
    },
    {
      found: 1,
      pos: { x: 450560, y: 819200, z: 0 },
      powerup: 0,
      dead: 0,
      battleStars: 0,
      coins: 0,
      tileProbe: {
        summary: { effectiveHoleAhead: 1, wallAhead: 0 },
        samples: [
          {
            found: 1,
            name: 'aheadFeet',
            worldX: 516096,
            worldY: 819200,
            tileId: 25,
            behavior: '0x00080002',
            solidish: 1,
            tile: { breakableBlock: 1 },
            block: {
              any: 1,
              breakable: 1,
              visibleStorageBreakableCandidate: 1,
            },
          },
        ],
        grid: {
          encoding: 'sparse_non_empty',
          height: 17,
          loggedCells: 3,
          totalCells: 561,
          width: 33,
          cells: [
            {
              behavior: '0x00050007',
              block: { any: 1, itemBox: 1, question: 1, storageContents: 7 },
              col: 17,
              pixelX: 126,
              pixelY: 200,
              relTileX: 1,
              relTileY: 0,
              row: 10,
              solidish: 1,
              tile: { questionBlock: 1 },
              tileId: 71,
            },
            {
              behavior: '0x00080002',
              block: {
                any: 1,
                breakable: 1,
                visibleStorageBreakableCandidate: 1,
              },
              col: 16,
              pixelX: 110,
              pixelY: 216,
              relTileX: 0,
              relTileY: 1,
              row: 11,
              solidish: 1,
              tile: { breakableBlock: 1 },
              tileId: 25,
            },
            {
              behavior: '0x00000000',
              block: { any: 0 },
              col: 18,
              pixelX: 142,
              pixelY: 184,
              relTileX: 2,
              relTileY: -1,
              row: 9,
              solidish: 0,
              tile: { coin: 1 },
              tileId: 0,
            },
          ],
        },
      },
    },
  ],
  objectSummary: { active: 2 },
  visualSummary: {
    categoryCounts: { coin: 1, player: 2 },
    visibleCamera0: 2,
    visibleCamera1: 2,
  },
  objects: [
    {
      category: 'coin',
      objectId: '0x0042',
      settings: '0x00000000',
      pos: { x: 458752, y: 806912, z: 0 },
    },
  ],
});

const recordingManifest = JSON.stringify({
  schema: 'nsmb_mvl_ai_recording_manifest_v1',
  kind: 'human',
  labelSource: 'player',
  quality: { status: 'unreviewed' },
  summary: {
    eventSamples: {
      playerDeath: [{ frame: 1200, player: 1 }],
      blockCandidateVisible: [
        {
          frame: 1230,
          itemBox: true,
          player: 1,
          sample: 'leftBody',
          storageContents: 7,
          tileId: 71,
        },
      ],
    },
  },
});

describe('AIログビューア', () => {
  test('JSONLを読み込んでフレームと相対配置を表示する', async () => {
    const screen = await render(
      <Tabs.Root value="ai">
        <AIReplayViewer />
      </Tabs.Root>,
    );

    const input =
      document.querySelector<HTMLInputElement>('input[type="file"]');
    expect(input).not.toBeNull();
    const files = new DataTransfer();
    files.items.add(
      new File([`${playlogLine}\n`], 'playlog.jsonl', {
        type: 'application/jsonl',
      }),
    );
    Object.defineProperty(input, 'files', {
      configurable: true,
      value: files.files,
    });
    input?.dispatchEvent(new Event('change', { bubbles: true }));

    await expect.element(screen.getByText(/index 0/)).toBeVisible();
    await expect.element(screen.getByText('RIGHT+Y')).toBeVisible();
    await expect.element(screen.getByTestId('ai-replay-scene')).toBeVisible();
    await expect
      .element(screen.getByText('coin', { exact: true }))
      .toBeVisible();
    await expect.element(screen.getByText(/breakable:1/)).toBeVisible();
  });

  test('複数行のai-playlog JSONLを読み込める', async () => {
    const screen = await render(
      <Tabs.Root value="ai">
        <AIReplayViewer />
      </Tabs.Root>,
    );

    const input =
      document.querySelector<HTMLInputElement>('input[type="file"]');
    expect(input).not.toBeNull();
    const secondLine = playlogLine.replace('"frame":900', '"frame":930');
    const files = new DataTransfer();
    files.items.add(
      new File([`${playlogLine}\n${secondLine}\n`], 'ai-playlog.jsonl', {
        type: 'application/jsonl',
      }),
    );
    Object.defineProperty(input, 'files', {
      configurable: true,
      value: files.files,
    });
    input?.dispatchEvent(new Event('change', { bubbles: true }));

    await expect.element(screen.getByText(/2 frames/)).toBeVisible();
    await expect
      .element(screen.getByText(/frame 900 \/ index 0/))
      .toBeVisible();
  });

  test('recording manifestのイベントsampleを表示する', async () => {
    const screen = await render(
      <Tabs.Root value="ai">
        <AIReplayViewer />
      </Tabs.Root>,
    );

    const input =
      document.querySelector<HTMLInputElement>('input[type="file"]');
    expect(input).not.toBeNull();
    const files = new DataTransfer();
    files.items.add(
      new File([recordingManifest], 'recording.json', {
        type: 'application/json',
      }),
    );
    Object.defineProperty(input, 'files', {
      configurable: true,
      value: files.files,
    });
    input?.dispatchEvent(new Event('change', { bubbles: true }));

    await expect.element(screen.getByText(/2 events/)).toBeVisible();
    await expect.element(screen.getByText('P1 death')).toBeVisible();
    await expect
      .element(screen.getByText(/P1 block leftBody tile 0x47 storage 7/))
      .toBeVisible();
    await expect.element(screen.getByText(/manifest human/)).toBeVisible();
  });

  test('Workbenchのpreview成果物を表示する', async () => {
    const screen = await render(
      <Tabs.Root value="ai">
        <AIReplayViewer />
      </Tabs.Root>,
    );

    await expect.element(screen.getByText('AI Workbench')).toBeVisible();
    await expect.element(screen.getByText('AI成果物')).toBeVisible();
    await expect.element(screen.getByText(/playlog \//)).toBeVisible();
  });
});
