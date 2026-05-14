-- ending_test.lua
-- 테스트용: F5 키를 누르면 엔딩 대화 시퀀스를 트리거한다.
-- 액터에 붙여두고 Play 모드에서 F5를 눌러 확인.

local KEY_F5 = 0x74
local triggered = false

function BeginPlay(owner)
    Log("[EndingTest] 준비 완료 - F5 키로 엔딩 대화를 테스트합니다.")
end

function Tick(owner, deltaTime)
    if triggered then return end

    if GetKeyDown(KEY_F5) or GetUIState() == "Ending" then
        triggered = true
        StartCoroutine(TriggerEnding)
    end
end

function TriggerEnding()
    Log("[EndingTest] 엔딩 시퀀스 시작")

    local type = GetEndingType()
    Log("[EndingTest] 엔딩 타입: " .. type)

    -- Ending 상태로 전환 (어두운 배경 표시)
    SetUIState("Ending")

    -- 대화 큐 등록 (타입에 따라 다름)
    if type == "Good" then
        QueueDialogue("내레이터", "당신의 노력 덕분에 세상은 다시 빛을 찾았습니다.")
        QueueDialogue("내레이터", "사람들은 다시 웃기 시작했고, 희망이 가득합니다.")
        QueueDialogue("주인공",   "이것이... 내가 바랐던 평화인가.")
    elseif type == "Normal" then
        QueueDialogue("내레이터", "큰 폭풍은 지나갔지만, 여전히 상처는 남아있습니다.")
        QueueDialogue("내레이터", "그래도 삶은 계속됩니다. 조금씩, 천천히.")
        QueueDialogue("주인공",   "...이제 쉴 수 있는 건가.")
    elseif type == "Bad" then
        QueueDialogue("내레이터", "어둠은 걷히지 않았습니다. 오히려 더 깊어졌을지도 모릅니다.")
        QueueDialogue("내레이터", "당신이 남긴 흔적들은 차갑게 식어갑니다.")
        QueueDialogue("주인공",   "결국... 아무것도 바뀌지 않았어.")
    else
        QueueDialogue("내레이터", "오랜 싸움이 끝났다.")
        QueueDialogue("내레이터", "폐허가 된 도시 위로, 새벽빛이 스며들었다.")
        QueueDialogue("주인공",   "...이제 쉴 수 있는 건가.")
        QueueDialogue("내레이터", "그리고 세계는 다시, 천천히 숨을 쉬기 시작했다.")
    end

    -- 모든 대화가 끝날 때까지 대기
    while IsDialogueActive() do
        wait(0.1)
    end

    Log("[EndingTest] 대화 종료 - THE END 표시 중")
end
