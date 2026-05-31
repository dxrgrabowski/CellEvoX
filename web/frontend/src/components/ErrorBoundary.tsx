import { Component, type ErrorInfo, type ReactNode } from 'react';

interface Props {
  children: ReactNode;
}

interface State {
  error: Error | null;
}

export default class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    console.error('Render error caught by ErrorBoundary', error, errorInfo);
  }

  render() {
    if (this.state.error) {
      return (
        <div className="error-boundary">
          <h3>Something went wrong while rendering this view.</h3>
          <p>{this.state.error.message}</p>
          <button className="btn btn--ghost" onClick={() => this.setState({ error: null })}>
            Try again
          </button>
        </div>
      );
    }

    return this.props.children;
  }
}
