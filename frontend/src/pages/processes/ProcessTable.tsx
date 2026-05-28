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
import type { ProcessInfo } from '../../types/process';
import styles from './ProcessTable.module.css';

const columnHelper = createColumnHelper<ProcessInfo>();

const columns = [
  columnHelper.accessor('pid', {
    header: 'PID',
    cell: (info) => <span className={styles.mono}>{info.getValue()}</span>,
    size: 70,
  }),
  columnHelper.accessor('user', {
    header: '用户',
    size: 80,
  }),
  columnHelper.accessor('name', {
    header: '进程名',
    size: 150,
  }),
  columnHelper.accessor('state', {
    header: '状态',
    size: 30,
    cell: (info) => {
      const s = info.getValue();
      return (
        <span className={`${styles.state} ${styles[`state_${s}`] || ''}`}>
          {s}
        </span>
      );
    },
  }),
  columnHelper.accessor('cpu_percent', {
    header: 'CPU%',
    cell: (info) => {
      const v = info.getValue();
      return <span className={styles.mono}>{v.toFixed(1)}</span>;
    },
    size: 70,
  }),
  columnHelper.accessor('rss_human', {
    header: '物理内存',
    size: 90,
    cell: (info) => <span className={styles.mono}>{info.getValue()}</span>,
  }),
  columnHelper.accessor('vsize_human', {
    header: '虚拟内存',
    size: 90,
    cell: (info) => <span className={styles.mono}>{info.getValue()}</span>,
  }),
  columnHelper.accessor('thread_count', {
    header: '线程',
    size: 50,
    cell: (info) => <span className={styles.mono}>{info.getValue()}</span>,
  }),
  columnHelper.accessor('cmdline', {
    header: '命令行',
    size: 300,
    cell: (info) => (
      <span className={styles.cmdline}>{info.getValue() || '--'}</span>
    ),
  }),
];

interface Props {
  processes: ProcessInfo[];
  onSelect: (pid: number) => void;
}

export default function ProcessTable({ processes, onSelect }: Props) {
  const [sorting, setSorting] = useState<SortingState>([{ id: 'cpu_percent', desc: true }]);
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
        p.name.toLowerCase().includes(q) ||
        p.user.toLowerCase().includes(q) ||
        (p.cmdline || '').toLowerCase().includes(q)
      );
    },
  });

  return (
    <div className={styles.container}>
      <div className={styles.toolbar}>
        <input
          className={styles.search}
          placeholder="搜索 PID、进程名、用户或命令..."
          value={globalFilter}
          onChange={(e) => setGlobalFilter(e.target.value)}
        />
        <span className={styles.count}>
          {table.getFilteredRowModel().rows.length} / {processes.length} 个进程
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
