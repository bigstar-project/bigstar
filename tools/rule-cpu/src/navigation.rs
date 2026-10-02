use crate::observation::{array, flag, int, num, solid, wrap};
use serde::Serialize;
use serde_json::Value;
use std::collections::HashMap;

type Tile = (i64, i64);
#[derive(Clone, Debug, Serialize)]
pub struct Waypoint {
    pub x: f64,
    pub depth: f64,
    pub jump: bool,
    pub path: Vec<Tile>,
}
#[derive(Clone)]
pub struct GrassNavigator {
    // Updating a tile must retain insertion order: the Python reference uses
    // dict order to break equal-cost routes and candidate targets.
    pub tiles: Vec<(Tile, i64)>,
    tile_index: HashMap<Tile, usize>,
    pub boxes: Vec<Tile>,
    pub box_active: HashMap<Tile, bool>,
    pub nodes: Vec<Tile>,
    edges: Vec<Vec<(usize, f64, bool)>>,
}
impl Default for GrassNavigator {
    fn default() -> Self {
        Self::from_json(include_str!("../grass-navigation-tiles.json"))
            .expect("embedded grass terrain")
    }
}
impl GrassNavigator {
    pub fn signature(&self) -> Vec<(i64, i64)> {
        let mut keys: Vec<_> = self
            .tiles
            .iter()
            .filter_map(|&(key, mask)| solid(Some(mask)).then_some(key))
            .collect();
        keys.sort_unstable();
        keys
    }
    pub fn from_json(source: &str) -> Result<Self, serde_json::Error> {
        let spec: Value = serde_json::from_str(source)?;
        let mut nav = Self {
            tiles: vec![],
            tile_index: HashMap::new(),
            boxes: vec![],
            box_active: HashMap::new(),
            nodes: vec![],
            edges: vec![],
        };
        for c in array(&spec["cells"]) {
            nav.set_tile((int(&c["x"]), int(&c["y"])), int(&c["mask"]));
        }
        for p in array(&spec["powerup_boxes"]) {
            let key = (int(&p[0]), int(&p[1]));
            nav.boxes.push(key);
            nav.box_active.insert(key, true);
        }
        nav.rebuild();
        Ok(nav)
    }
    pub fn mask(&self, key: Tile) -> Option<i64> {
        self.tile_index.get(&key).map(|&i| self.tiles[i].1)
    }
    pub fn set_tile(&mut self, key: Tile, mask: i64) {
        if let Some(&index) = self.tile_index.get(&key) {
            self.tiles[index].1 = mask;
        } else {
            self.tile_index.insert(key, self.tiles.len());
            self.tiles.push((key, mask));
        }
    }
    pub fn occupied(&self, x: f64, depth: f64) -> bool {
        solid(self.mask((
            ((x / 16.0).floor() as i64).rem_euclid(64),
            (depth / 16.0).floor() as i64,
        )))
    }
    pub fn can_walk_down(&self, x: f64, depth: f64, target_x: f64, target_depth: f64) -> bool {
        if target_depth <= depth + 24.0 {
            return false;
        }
        let dx = wrap(target_x, x);
        [0.2, 0.4, 0.6, 0.8, 1.0].iter().all(|t| {
            ((depth as i64 + 8)..(target_depth as i64 + 17))
                .step_by(8)
                .any(|y| self.occupied(x + dx * t, y as f64))
        })
    }
    pub fn rebuild(&mut self) {
        self.nodes = self
            .tiles
            .iter()
            .filter_map(|&((x, y), mask)| {
                ((5..=21).contains(&y)
                    && solid(Some(mask))
                    && !self.occupied((x * 16 + 8) as f64, (y * 16 - 8) as f64)
                    && !self.occupied((x * 16 + 8) as f64, (y * 16 - 24) as f64))
                .then_some((x * 16 + 8, y * 16))
            })
            .collect();
        self.edges = self
            .nodes
            .iter()
            .map(|&p| {
                self.nodes
                    .iter()
                    .enumerate()
                    .filter_map(|(i, &q)| {
                        let offset = wrap(q.0 as f64, p.0 as f64);
                        let fall = (q.1 - p.1) as f64;
                        if p == q
                            || offset.abs() > 144.0
                            || !(-64.0..=112.0).contains(&fall)
                            || (fall < 0.0 && offset.abs() > 112.0 + 0.85 * fall)
                        {
                            return None;
                        }
                        if fall == 0.0
                            && offset.abs() <= 32.0
                            && [0.25, 0.5, 0.75]
                                .iter()
                                .all(|t| self.occupied(p.0 as f64 + offset * t, (p.1 + 8) as f64))
                        {
                            return Some((i, offset.abs(), false));
                        }
                        let apex = 40.0_f64.max(-fall + 12.0);
                        let clear = (1..16).all(|j| {
                            let t = j as f64 / 16.0;
                            let x = p.0 as f64 + offset * t;
                            let y = p.1 as f64 + fall * t - 4.0 * apex * t * (1.0 - t);
                            [-5.0, 5.0]
                                .iter()
                                .all(|s| [2.0, 20.0].iter().all(|h| !self.occupied(x + s, y - h)))
                        });
                        clear.then_some((i, offset.abs() + 45.0 + (-fall).max(0.0), true))
                    })
                    .collect()
            })
            .collect();
    }
    pub fn update(&mut self, player: &Value) {
        let grid = &player["terrain"];
        let ax = (num(&player["pos"]["x"]) / 65536.0).floor() as i64;
        let ay = (-num(&player["pos"]["y"]) / 65536.0).floor() as i64;
        let local: HashMap<_, _> = array(&grid["cells"])
            .iter()
            .map(|c| ((int(&c["rx"]), int(&c["ry"])), c))
            .collect();
        let mut changed = false;
        for rx in int(&grid["minRelTileX"])..int(&grid["minRelTileX"]) + int(&grid["width"]) {
            for ry in int(&grid["minRelTileY"])..int(&grid["minRelTileY"]) + int(&grid["height"]) {
                let cell = local.get(&(rx, ry));
                let (found, status, mask, behavior) =
                    cell.map_or((flag(&grid["omittedCellFound"]), 0, 0, 0), |c| {
                        (
                            flag(&c["found"]),
                            int(&c["status"]),
                            int(&c["mask"]),
                            int(&c["behavior"]),
                        )
                    });
                if !found || status != 0 {
                    continue;
                }
                let key = ((ax + rx).rem_euclid(64), ay + ry);
                if let Some(active) = self.box_active.get_mut(&key) {
                    *active = behavior == 0x50000;
                }
                changed |= solid(self.mask(key)) != solid(Some(mask));
                self.set_tile(key, mask);
            }
        }
        if changed {
            self.rebuild();
        }
    }
    pub fn route(
        &self,
        x: f64,
        depth: f64,
        goal_x: f64,
        goal_depth: f64,
        under: bool,
    ) -> Option<Waypoint> {
        let start = self
            .nodes
            .iter()
            .enumerate()
            .min_by(|(_, a), (_, b)| {
                let score =
                    |n: &&Tile| wrap(n.0 as f64, x).abs() + 3.0 * (n.1 as f64 - depth).abs();
                score(a).total_cmp(&score(b))
            })?
            .0;
        let mut distance = vec![f64::INFINITY; self.nodes.len()];
        let mut prev = vec![None; self.nodes.len()];
        // Small fixed graph: selecting the least tuple reproduces heapq's
        // (cost, (x,y)) ordering without floating-point Ord wrappers.
        let mut heap = vec![(0.0_f64, start)];
        let mut discovered = vec![start];
        distance[start] = 0.0;
        while !heap.is_empty() {
            let index = (0..heap.len())
                .min_by(|&a, &b| {
                    heap[a]
                        .0
                        .total_cmp(&heap[b].0)
                        .then(self.nodes[heap[a].1].cmp(&self.nodes[heap[b].1]))
                })
                .unwrap();
            let (cost, p) = heap.swap_remove(index);
            if cost != distance[p] {
                continue;
            }
            for &(q, w, jump) in &self.edges[p] {
                let new = cost + w;
                if new < distance[q] {
                    if distance[q].is_infinite() {
                        discovered.push(q);
                    }
                    distance[q] = new;
                    prev[q] = Some((p, jump));
                    heap.push((new, q));
                }
            }
        }
        let score = |&i: &usize| {
            let n = self.nodes[i];
            let v = n.1 as f64 - goal_depth;
            let mut height = if under {
                2.0 * v.abs()
            } else {
                2.0 * v.max(0.0) + 0.15 * (-v).max(0.0)
            };
            if !under
                && 0.0 < v
                && v <= 64.0
                && !(goal_depth as i64..n.1 - 8)
                    .step_by(8)
                    .any(|y| self.occupied(goal_x, y as f64))
            {
                height = 0.35 * v;
            }
            if v < -24.0
                && (n.1 + 1..goal_depth as i64 - 8)
                    .step_by(8)
                    .any(|y| self.occupied(goal_x, y as f64))
            {
                height += 240.0;
            }
            wrap(n.0 as f64, goal_x).abs() + height + distance[i] * 0.08
        };
        let goal = *discovered
            .iter()
            .min_by(|a, b| score(a).total_cmp(&score(b)))
            .unwrap();
        let mut indices = vec![goal];
        while *indices.last().unwrap() != start {
            indices.push(prev[*indices.last().unwrap()].unwrap().0);
        }
        indices.reverse();
        let path = indices.iter().map(|&i| self.nodes[i]).collect();
        if indices.len() == 1 {
            return Some(Waypoint {
                x: goal_x,
                depth: goal_depth,
                jump: false,
                path,
            });
        }
        let mut i = 1;
        while i + 1 < indices.len()
            && !prev[indices[i]].unwrap().1
            && !prev[indices[i + 1]].unwrap().1
        {
            i += 1;
        }
        let n = self.nodes[indices[i]];
        Some(Waypoint {
            x: n.0 as f64,
            depth: n.1 as f64,
            jump: prev[indices[i]].unwrap().1,
            path,
        })
    }
}
