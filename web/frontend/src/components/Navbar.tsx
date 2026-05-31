import { NavLink } from 'react-router-dom';
import { Activity, Settings, BarChart2, FlaskConical } from 'lucide-react';
import { useSimStore } from '../stores';

const NAV_ITEMS = [
  { to: '/config', label: 'Configure', icon: Settings },
  { to: '/run',    label: 'Run',       icon: Activity },
  { to: '/results',label: 'Results',   icon: BarChart2 },
];

export default function Navbar() {
  const status = useSimStore(s => s.status.status);

  return (
    <header style={{
      position: 'sticky', top: 0, zIndex: 100,
      height: '64px',
      background: 'rgba(7,12,20,0.85)',
      backdropFilter: 'blur(20px)',
      borderBottom: '1px solid var(--border)',
      display: 'flex', alignItems: 'center',
      padding: '0 32px',
      gap: '32px',
    }}>
      {/* Logo */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '10px', textDecoration: 'none' }}>
        <div style={{
          width: 36, height: 36,
          borderRadius: '10px',
          background: 'linear-gradient(135deg, var(--accent), var(--purple))',
          display: 'grid', placeItems: 'center',
          boxShadow: 'var(--accent-glow)',
        }}>
          <FlaskConical size={18} color="#070c14" strokeWidth={2.5} />
        </div>
        <span style={{ fontWeight: 700, fontSize: '1.1rem', letterSpacing: '-0.02em' }}>
          Cell<span style={{ color: 'var(--accent)' }}>Evo</span>X
        </span>
      </div>

      {/* Navigation */}
      <nav style={{ display: 'flex', gap: '4px', flex: 1 }}>
        {NAV_ITEMS.map(({ to, label, icon: Icon }) => (
          <NavLink
            key={to}
            to={to}
            style={({ isActive }) => ({
              display: 'flex', alignItems: 'center', gap: '6px',
              padding: '6px 14px',
              borderRadius: 'var(--radius-md)',
              fontSize: '0.875rem',
              fontWeight: 500,
              textDecoration: 'none',
              color: isActive ? 'var(--accent)' : 'var(--text-secondary)',
              background: isActive ? 'var(--accent-dim)' : 'transparent',
              transition: 'all var(--transition)',
            })}
          >
            {({ isActive }) => (
              <>
                <Icon size={15} strokeWidth={isActive ? 2.5 : 2} />
                {label}
              </>
            )}
          </NavLink>
        ))}
      </nav>

      {/* Status badge */}
      <span className={`badge badge--${status}`}>
        <span style={{
          width: 6, height: 6, borderRadius: '50%',
          background: status === 'running' ? 'var(--accent)' : 'currentColor',
          display: 'inline-block',
          animation: status === 'running' ? 'blink 1s step-end infinite' : 'none',
        }} />
        {status}
      </span>
    </header>
  );
}
