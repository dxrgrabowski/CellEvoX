import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import Navbar from './components/Navbar';
import ConfigPage from './pages/ConfigPage';
import RunPage from './pages/RunPage';
import ResultsPage from './pages/ResultsPage';
import ErrorBoundary from './components/ErrorBoundary';

const queryClient = new QueryClient({
  defaultOptions: {
    queries: { retry: 1, staleTime: 5000 },
  },
});

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <BrowserRouter>
        <div className="app-shell">
          <Navbar />
          <main>
            <ErrorBoundary>
              <Routes>
                <Route path="/"        element={<Navigate to="/config" replace />} />
                <Route path="/config"  element={<ConfigPage />} />
                <Route path="/run"     element={<RunPage />} />
                <Route path="/results" element={<ResultsPage />} />
              </Routes>
            </ErrorBoundary>
          </main>
        </div>
      </BrowserRouter>
    </QueryClientProvider>
  );
}
