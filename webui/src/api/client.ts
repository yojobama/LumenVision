import createClient from 'openapi-fetch';
import type { paths } from './generated';

// Typed OpenAPI client; base URL is relative to the page origin.
export const apiClient = createClient<paths>({ baseUrl: `${window.location.origin}/api` });
