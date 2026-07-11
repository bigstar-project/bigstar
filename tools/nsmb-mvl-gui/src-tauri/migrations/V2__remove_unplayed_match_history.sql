DELETE FROM match_history
WHERE NOT EXISTS (
  SELECT 1
  FROM match_stage_results
  WHERE match_stage_results.match_id = match_history.id
    AND match_stage_results.resolved = 1
    AND match_stage_results.winner IS NOT NULL
);
