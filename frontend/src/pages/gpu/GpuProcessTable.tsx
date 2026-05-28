import { useState } from 'react';
import {
  useReactTable,
  getCoreRowModel,
  getSortedRowModel,
  getFilteredRowModel,
  createColumnHelper,
  flexRender,
  type SortingState,
} from '@tanstack/react-table';
import type { GpuProcessInfo } from '../../types/gpu';
import styles from './GpuProcessTable.module.css';

const columnHelper = createColumnHelper<GpuProcessInfo>();

const columns = [
  columnHelper.accessor('gpu_index', {
    header: 'GPU',
    size: 50,
    cell: (info) => <span className={styles.mono}>{info.getValue()}</span>,
  }),
  columnHelper.accessor('pid', {
    header: 'PID',
    size: 70,
    cell: (info) => <span className={styles.mono}>{info.getValue()}</span>,
  }),
  columnHelper.accessor('name', {
    header: '进程名',
    size: 120,
    cell: (info) => info.getValue() || '--',
  }),
  columnHelper.accessor('user', {
    header: '用户',
    size: 80,
    cell: (info) => info.getValue() || '--',
  }),
  columnHelper.accessor('process_type', {
    header: '类型',
    size: 50,
    cell: (info) => {
      const t = info.getValue();
      return <span className={`${styles.type} ${t.includes('C') ? styles.typeC : ''} ${t.includes('G') ? styles.typeG : ''}`}>{t}</span>;
    },
  }),
  columnHelper.accessor('gpu_memory_bytes', {
    header: 'GPU显存',
    size: 100,
    cell: (info) => {
      const b = info.getValue();
      if (b >= 1073741824) return <span className={styles.mono}>{(b / 1073741824).toFixed(1)} GB</span>;
      return <span className={styles.mono}>{(b / 1048576).toFixed(0)} MB</span>;
    },
  }),
  columnHelper.accessor('cpu_percent', {
    header: 'CPU%',
    size: 60,
    cell: (info) => {
      const v = info.getValue();
      if (v === undefined) return <span className={styles.na}>--</span>;
      return <span className={styles.mono}>{v.toFixed(1)}</span>;
    },
  }),
  columnHelper.accessor('cmdline', {
    header: '命令行',
    size: 250,
    cell: (info) => (
      <span className={styles.cmdline}>{info.getValue() || '--'}</span>
    ),
  }),
];

interface Props {
  processes: GpuProcessInfo[];
  onSelect: (pid: number) => void;
}

export default function GpuProcessTable({ processes, onSelect }: Props) {
  const [sorting, setSorting] = useState<SortingState>([{ id: 'gpu_memory_bytes', desc: true }]);
  const [globalFilter, setGlobalFilter] = useState('');

  const table = useReactTable({
    data: processes,
    columns,
    state: { sorting, globalFilter },
    onSortingChange: setSorting,
    onGlobalFilterChange: setGlobalFilter,
    getCoreRowModel: getCoreRowModel(),
    getSortedRowModel: getSortedRowModel(),
    getFilteredRowModel: getFilteredRowModel(),
    globalFilterFn: (row, _id, query) => {
      const q = query.toLowerCase();
      const p = row.original;
      return (
        String(p.pid).includes(q) ||
        (p.name || '').toLowerCase().includes(q) ||
        (p.user || '').toLowerCase().includes(q) ||
        (p.cmdline || '').toLowerCase().includes(q)
      );
    },
  });

  return (
    <div className={styles.container}>
      <div className={styles.toolbar}>
        <span className={styles.title}>GPU 进程</span>
        <input
          className={styles.search}
          placeholder="搜索 PID、进程名、用户..."
          value={globalFilter}
          onChange={(e) => setGlobalFilter(e.target.value)}
        />
        <span className={styles.count}>
          {table.getFilteredRowModel().rows.length} 个进程
        </span>
      </div>
      <div className={styles.tableWrap}>
        <table className={styles.table}>
          <thead>
            {table.getHeaderGroups().map((hg) => (
              <tr key={hg.id}>
                {hg.headers.map((header) => (
                  <th
                    key={header.id}
                    style={{ width: header.getSize() }}
                    className={styles.th}
                    onClick={header.column.getToggleSortingHandler()}
                  >
                    {flexRender(header.column.columnDef.header, header.getContext())}
                    {{ asc: ' ^', desc: ' v' }[header.column.getIsSorted() as string] ?? ''}
                  </th>
                ))}
              </tr>
            ))}
          </thead>
          <tbody>
            {table.getRowModel().rows.map((row) => (
              <tr
                key={row.id}
                className={styles.row}
                onClick={() => onSelect(row.original.pid)}
              >
                {row.getVisibleCells().map((cell) => (
                  <td key={cell.id} className={styles.td}>
                    {flexRender(cell.column.columnDef.cell, cell.getContext())}
                  </td>
                ))}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
