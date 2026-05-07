function BeginPlay(owner)
    if owner == nil then
        print("[Item][Lua] MedicineBottle BeginPlay owner=nil")
        return
    end

    local itemId = "medicine_bottle"
    local registered = RegisterItemActor(owner, itemId)
    print("[Item][Lua] Register " .. itemId .. " actor=" .. owner:GetName() .. " result=" .. tostring(registered))
end
