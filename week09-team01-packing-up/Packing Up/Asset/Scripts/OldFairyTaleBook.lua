function BeginPlay(owner)
    if owner == nil then
        print("[Item][Lua] OldFairyTaleBook BeginPlay owner=nil")
        return
    end

    local itemId = "old_fairy_tale_book"
    local registered = RegisterItemActor(owner, itemId)
    print("[Item][Lua] Register " .. itemId .. " actor=" .. owner:GetName() .. " result=" .. tostring(registered))
end
