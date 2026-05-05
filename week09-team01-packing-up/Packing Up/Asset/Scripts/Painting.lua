function BeginPlay(owner)
    if owner == nil then
        print("[Item][Lua] Painting BeginPlay owner=nil")
        return
    end

    local itemId = "painting"
    local registered = RegisterItemActor(owner, itemId)
    print("[Item][Lua] Register " .. itemId .. " actor=" .. owner:GetName() .. " result=" .. tostring(registered))
end
