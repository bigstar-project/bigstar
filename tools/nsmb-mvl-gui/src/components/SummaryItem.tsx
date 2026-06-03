export function SummaryItem({
  label,
  value,
}: {
  label: string;
  value: string;
}) {
  return (
    <div className="grid gap-0.5">
      <span className="text-xs font-black text-slate-500">{label}</span>
      <strong className="overflow-wrap-anywhere text-sm text-slate-100">
        {value}
      </strong>
    </div>
  );
}
