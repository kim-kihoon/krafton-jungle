function BeginPlay(owner)
    if owner == nil then
        print("[Item][Lua] CrumpledReceipt BeginPlay owner=nil")
        return
    end

    local itemId = "crumpled_paper"
    local registered = RegisterItemActor(owner, itemId)
    print("[Item][Lua] Register " .. itemId .. " actor=" .. owner:GetName() .. " result=" .. tostring(registered))
end
