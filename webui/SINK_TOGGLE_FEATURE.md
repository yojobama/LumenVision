# Sink Toggle Feature Implementation

## Overview
This document describes the implementation of the sink toggle feature that allows users to manually enable or disable sinks through the web interface.

## Changes Made

### 1. Type Definitions (`src/types/index.ts`)
- Added `isEnabled?: boolean` property to the `Sink` interface to track the enabled/disabled state of each sink

### 2. API Service (`src/services/ApiService.ts`)
Added the following methods to interact with the sink toggle endpoints:

- **`getSinkStatus(sinkId: number): Promise<boolean>`**
  - Calls `GET /api/sink/getStatus?SinkID={sinkId}`
  - Returns the current enabled/disabled status of a sink

- **`toggleSink(sinkId: number, enabled: boolean): Promise<void>`**
  - Calls `PATCH /api/sink/toggle?SinkID={sinkId}&Enabled={enabled}`
  - Toggles the sink's enabled/disabled state

- Updated existing `enableSink` and `disableSink` methods to use the new toggle endpoint

### 3. Data Hook (`src/hooks/useAppData.ts`)
- **Modified `loadData` function**:
  - Now fetches the enabled/disabled status for each sink using `api.getSinkStatus()`
  - Stores the status in the `isEnabled` property of each sink

- **Added `handleToggleSink` function**:
  - Takes `sinkId` and `enabled` parameters
  - Calls the API to toggle the sink
  - Updates local state immediately for better UX
  - Shows success/error toast messages
  - Reloads data on error to sync with server state

- Exported `handleToggleSink` for use in components

### 4. UI Components (`src/App.tsx`)

#### New ToggleSwitch Component
Created a reusable toggle switch component with:
- Green background when enabled, grey when disabled
- Animated sliding toggle button
- Hover and focus states
- Disabled state support
- Accessible with title tooltips

#### Updated DashboardPage
- Added `onToggleSink` prop
- Integrated toggle switch into sink cards in the dashboard
- Toggle switch appears alongside the "LIVE" indicator and stream controls

#### Updated SinksPage
- Added `onToggleSink` prop
- Added toggle switch with status label showing "Enabled" or "Disabled"
- Toggle switch prominently displayed with clear visual feedback

#### Updated Main App Component
- Destructured `handleToggleSink` from the `useAppData` hook
- Passed `handleToggleSink` to both `DashboardPage` and `SinksPage`

## User Experience

### Toggle Switch Behaviour
1. **Visual Feedback**: 
   - Enabled: Green background with toggle on the right
   - Disabled: Grey background with toggle on the left
   - Smooth animation when switching states

2. **Interaction**:
   - Click the toggle switch to change state
   - Immediate UI update for responsive feel
   - Toast notification confirms the action

3. **Error Handling**:
   - If toggle fails, shows error message
   - Automatically reloads data to show actual server state

### Where Toggles Appear
1. **Dashboard Page**: 
   - In the "Processing Sinks" widget
   - Next to each sink in the recent sinks list
   - Alongside stream controls

2. **Sinks Page**:
   - In each sink card
   - With a labelled status ("Enabled" / "Disabled")
   - Above the stream control buttons

## API Endpoints Used

### GET /api/sink/getStatus
- **Query Parameters**: `SinkID` (int)
- **Returns**: `boolean` - true if the sink is enabled, false if it is disabled
- **Called**: When loading sink data to display current status

### PATCH /api/sink/toggle
- **Query Parameters**: 
  - `SinkID` (int) - The ID of the sink to toggle
  - `Enabled` (bool) - true to enable, false to disable
- **Returns**: No content (204)
- **Called**: When the user clicks the toggle switch

## Testing Recommendations

1. **Basic Toggle**: Verify that clicking the toggle switch changes the sink state
2. **Status Persistence**: Verify that sink status is maintained after page refresh
3. **Multiple Sinks**: Test toggling multiple sinks and verify each maintains its own state
4. **Error Handling**: Test behaviour when the API is unavailable or returns errors
5. **Visual States**: Verify the toggle switch displays correctly in both light and dark modes

## Future Enhancements

Possible improvements for future iterations:
1. Batch toggle (enable/disable all sinks at once)
2. Keyboard shortcuts for toggling
3. Visual indication when a sink is processing (in addition to enabled state)
4. Confirmation dialog for critical sinks before disabling
5. Display sink toggle history/logs

## Notes

- No changes were made to the C# server or C++ library as requested
- The feature integrates seamlessly with existing sink management functionality
- Toggle state is fetched from the server on each data refresh to ensure accuracy
- Local state updates provide immediate feedback while API calls are processed
