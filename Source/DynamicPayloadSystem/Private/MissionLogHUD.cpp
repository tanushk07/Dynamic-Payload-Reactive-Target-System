#include "MissionLogHUD.h"

void AMissionLogHUD::PushGameLog_Implementation(const FGameLogEntry& Entry)
{
	if (MaxLogHistory > 0)
	{
		LogHistory.Add(Entry);

		// Trim from the front so the newest entries survive. Unbounded growth
		// here would be a slow leak across a long session.
		const int32 Excess = LogHistory.Num() - MaxLogHistory;
		if (Excess > 0)
		{
			LogHistory.RemoveAt(0, Excess);
		}
	}

	OnMissionLog.Broadcast(Entry);
}

FString AMissionLogHUD::GetLastMessage() const
{
	return LogHistory.Num() > 0 ? LogHistory.Last().Message.ToString() : FString();
}
