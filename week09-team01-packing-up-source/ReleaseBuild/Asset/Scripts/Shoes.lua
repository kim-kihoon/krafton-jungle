local shellDropped = false

function BeginPlay(owner)
    if owner == nil then
        print("[Item][Lua] Shoes BeginPlay owner=nil")
        return
    end

    local itemId = "shoes"
    local registered = RegisterItemActor(owner, itemId)
    print("[Item][Lua] Register " .. itemId .. " actor=" .. owner:GetName() .. " result=" .. tostring(registered))
end

function OnPickedUp(owner, picker)
    if owner == nil or shellDropped then
        return
    end

    shellDropped = true
    StartCoroutine(function()
        wait(1.0)
        local dropped = DropRegisteredItemFromActor("shell", owner, FVector.new(-0.5, -0.1, 0.4))
        print("[Item][Lua] Drop shell result=" .. tostring(dropped))
        if not dropped then
            shellDropped = false
        end
    end)
end
