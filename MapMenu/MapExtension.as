import gfx.managers.FocusHandler;
import gfx.io.GameDelegate;
import gfx.ui.InputDetails;
import Components.CrossPlatformButtons;
import Shared.GlobalFunc;
import gfx.ui.NavigationCode;
import flash.geom.Rectangle;
import flash.geom.Transform;
import flash.geom.ColorTransform;
import skse;
import DxScanToWindows;

class MapExtension extends MovieClip
{
	#include "../version.as"
	
	var TamrielMap_mc:MovieClip;
	var TamrielMap:MovieClip;
	var TamrielTacticalMap_mc:MovieClip;
	var TamrielTacticalMap:MovieClip;
	var WorldOptionsHolder_mc:MovieClip;
	var WorldOptionsHolder:MovieClip;
	var DescriptionHolder_mc:MovieClip;
	var RegionSelector_mc:MovieClip;
	var RegionSelector:MovieClip;
	var RegionSelectorScreen_mc:MovieClip;
	var RegionSelectorScreen:MovieClip;
	var LocStatsHolder_mc:MovieClip;
	var QuestsOngoingStatsHolder_mc:MovieClip;
	var QuestsCompletedStatsHolder_mc:MovieClip;
	var AllegianceHolder_mc:MovieClip;
	var SeasonHolder_mc:MovieClip;
	var BountyHolder_mc:MovieClip;
	var SurvivalHolder_mc:MovieClip;
	var ViewSelector_mc:MovieClip;
	var Blocker_mc:MovieClip;
	
	var bGamepad:Boolean;
	var bSeasons:Boolean;
	
	var showRegionsButton;
	var showRegionsText;	
	var changeModeButton;
	var changeModeText;
	
	var menuHotkey:Number;
	var menuGamepadHotkey:Number;
	var regionsKey:Number;
	var regionsGamepadKey:Number;
	var modeKey:Number;
	var modeGamepadKey:Number;
	var leftKey:Number;
	var leftGamepadKey:Number;
	var rightKey:Number;
	var rightGamepadKey:Number;
	
	var iLeftmostOption:Number = 0;
	var sCurrentProvince:String = "";
	var iCurrentTacticalView:Number = 0;
	var aTacticalViews = new Array();
	
	private var _platform:Number;
	
	private var _deleteControls:Object;
	private var _defaultControls:Object;
	private var _kinectControls:Object;
	private var _acceptControls:Object;
	private var _cancelControls:Object;
	private var _acceptButton:MovieClip;
	private var _cancelButton:MovieClip;
	private var _currentFocus:MovieClip;
	
	function MapExtension()
	{
		super();
		FocusHandler.instance.setFocus(this,0);
		Mouse.addListener(this);
		
		TamrielMap = TamrielMap_mc;
		TamrielTacticalMap = TamrielTacticalMap_mc;
		TamrielTacticalMap._visible = false;
		
		Blocker_mc.onRollOver = function() {};
		Blocker_mc.onRollOut = function() {};
		Blocker_mc.onMouseDown = function() {};
		
		WorldOptionsHolder = WorldOptionsHolder_mc;
		WorldOptionsHolder.symbol._visible = false;
		WorldOptionsHolder.symbol._xscale = 500;
		WorldOptionsHolder.symbol._yscale = 500;
		WorldOptionsHolder.textField.text = "$TAMRIEL";
		WorldOptionsHolder.textField.textAutoSize = "shrink";
		
		DescriptionHolder_mc.textAutoSize = "shrink";
		DescriptionHolder_mc.verticalAlign = "top";
		DescriptionHolder_mc.textField.text = "$TAMRIEL_DESC";
		
		aTacticalViews[0] = {name: "$ALLIANCES", func: ShowAlliances};
		aTacticalViews[1] = {name: "$CRIME", func: ShowCrime};
		aTacticalViews[2] = {name: "$SURVIVAL", func: ShowSurvival};
		ViewSelector_mc._visible = false;
		ViewSelector_mc.leftButton._alpha = 40;
		ViewSelector_mc.textField.text = aTacticalViews[0].name;
		
		LocStatsHolder_mc.title.text = "$DISCOVERED";
		QuestsOngoingStatsHolder_mc.title.text = "$QUESTS_ONGOING";
		QuestsCompletedStatsHolder_mc.title.text = "$QUESTS_COMPLETED";
		LocStatsHolder_mc._visible = false;
		QuestsOngoingStatsHolder_mc._visible = false;
		QuestsCompletedStatsHolder_mc._visible = false;
		AllegianceHolder_mc.title.text = "$ALLEGIANCE";
		AllegianceHolder_mc._visible = false;
		SeasonHolder_mc._visible = false;
		SeasonHolder_mc.title.text = "$CURRENT_SEASON";
		BountyHolder_mc.title.text = "$BOUNTY";
		BountyHolder_mc._visible = false;
		SurvivalHolder_mc.title.text = "$CLIMATE";
		SurvivalHolder_mc._visible = false;
		
		showRegionsText.textAutoSize = "shrink";
		showRegionsText.text = "$SHOW_REGIONS";
		showRegionsText._visible = false;
		showRegionsButton._visible = false;
		
		changeModeText.textAutoSize = "shrink";
		changeModeText.text = "$TACTICAL_VIEW";
		
		RegionSelectorScreen = RegionSelectorScreen_mc;
		RegionSelectorScreen.description.textField.text = "";
		RegionSelectorScreen.regionTitle.textField.text = "";
		RegionSelectorScreen.regionTitle.symbol._xscale = 500;
		RegionSelectorScreen.regionTitle.symbol._yscale = 500;
		RegionSelectorScreen.regionTitle._alpha = 0;
		RegionSelector = RegionSelectorScreen.RegionSelector_mc;
		RegionSelectorScreen.blocker.onRollOver = function() {};
		RegionSelectorScreen.blocker.onRollOut = function() {};
		RegionSelectorScreen.blocker.onMouseDown = function() {};
		RegionSelectorScreen._visible = false;
		RegionSelectorScreen.LocStatsHolder_mc._visible = false;
		RegionSelectorScreen.QuestsOngoingStatsHolder_mc._visible = false;
		RegionSelectorScreen.QuestsCompletedStatsHolder_mc._visible = false;
		RegionSelectorScreen.LocStatsHolder_mc.title.text = "$DISCOVERED";
		RegionSelectorScreen.QuestsOngoingStatsHolder_mc.title.text = "$QUESTS_ONGOING";
		RegionSelectorScreen.QuestsCompletedStatsHolder_mc.title.text = "$QUESTS_COMPLETED";

		var provinceName:String;
		for (provinceName in TamrielMap){
			TamrielMap[provinceName].name = "$"+provinceName.toUpperCase();
			TamrielMap[provinceName].description = "$"+provinceName.toUpperCase()+"_DESC";
			TamrielMap[provinceName].province = provinceName;
			TamrielMap[provinceName]._alpha = 60;
			TamrielMap[provinceName].onRollOver = function()
			{
				_parent._parent.onWorldHover(this);
			}
			TamrielMap[provinceName].onRollOut = function()
			{
				_parent._parent.onWorldOut(this);
			}
			TamrielMap[provinceName].onMouseDown = function()
			{
				if (Mouse.getTopMostEntity() == this){
					_parent._parent.onWorldPress(this);
				}
			}
		}
		
		var provinceName_t:String;
		var territoryName:String;
		for (provinceName_t in TamrielTacticalMap){
			for (territoryName in TamrielTacticalMap[provinceName_t]){
				var territory = TamrielTacticalMap[provinceName_t][territoryName];
				territory.name = "$"+territoryName.toUpperCase();
				//territory.description = "$"+provinceName.toUpperCase()+"_DESC";
				territory.territory = territoryName;
				territory._alpha = 75;
				territory.onRollOver = function()
				{
					_parent._parent._parent.onTerritoryHover(this);
				}
				territory.onRollOut = function()
				{
					_parent._parent._parent.onTerritoryOut(this);
				}
			}
		}
		
		for (var i = 0; i < 6; i++){
			RegionSelector["option"+i].textField.text = "";
			RegionSelector["option"+i]._visible = false;
			RegionSelector["option"+i].mask._alpha = 0;
			RegionSelector["option"+i].textField._alpha = 60;
			RegionSelector["option"+i].mask.onRollOver = function()
			{
				_parent._parent._parent._parent.onOptionHover(this._parent);
			}
			
			RegionSelector["option"+i].mask.onRollOut = function()
			{
				_parent._parent._parent._parent.onOptionOut(this._parent);
			}
			
			RegionSelector["option"+i].mask.onMouseDown = function()
			{
				if (Mouse.getTopMostEntity() == this)
				{
					_parent._parent._parent._parent.onOptionPress(this._parent);
				}
}
		}
		
		_platform = 0;
	}
	
		function onLoad()
	{
		SetPlatform(_platform, false);
	}
	
	function handleInput(details:InputDetails, pathToFocus:Array):Boolean
	{
		var bHandledInput:Boolean = false;
		if (GlobalFunc.IsKeyPressed(details))
		{
			if (details.navEquivalent == NavigationCode.TAB || details.code == menuHotkey || details.code == menuGamepadHotkey) {
				if (RegionSelectorScreen._visible == true){
					CloseRegionSelector();
				} else {
					CloseMenu();
				}
				bHandledInput = true;
			}
			else if (details.code == regionsKey || details.code == regionsGamepadKey) {
				if (showRegionsButton._visible){
					if (!RegionSelectorScreen._visible){
						showRegionsText.text = "$HIDE_REGIONS";
						var worldClip = TamrielMap[sCurrentProvince];
						
						worldClip._alpha = 60;
						RegionSelectorScreen._visible = true;
						
						var boxLength:Number;
						
						if (worldClip.province.toUpperCase() == worldClip.worlds[0].name) {
							for (var i = 0;i < 6; i++){
								if (worldClip.worlds[i+1] != undefined){
									RegionSelector["option"+i]._visible = true;
									RegionSelector["option"+i].textField.text = worldClip.worlds[i+1].name;
									RegionSelector["option"+i].id = worldClip.worlds[i+1].id;
									RegionSelector["option"+i].description = worldClip.worlds[i+1].description;
									RegionSelector["option"+i].symbol = worldClip.worlds[i+1].symbol;
									RegionSelector["option"+i].plugin = worldClip.worlds[i+1].plugin;
									boxLength = i;
								} else {
									RegionSelector["option"+i].textField.text = "";
									RegionSelector["option"+i]._visible = false;
								}
								RegionSelectorScreen.regionTitle._alpha = 0;
								RegionSelector["option"+i].mask._alpha = 0;
							}
						} else {
							for (var i = 0;i < 6; i++){
								if (worldClip.worlds[i] != undefined){
									RegionSelector["option"+i]._visible = true;
									RegionSelector["option"+i].textField.text = worldClip.worlds[i].name;
									RegionSelector["option"+i].id = worldClip.worlds[i].id;
									RegionSelector["option"+i].description = worldClip.worlds[i].description;
									RegionSelector["option"+i].symbol = worldClip.worlds[i].symbol;
									RegionSelector["option"+i].plugin = worldClip.worlds[i].plugin;
									boxLength = i;
								} else {
									RegionSelector["option"+i].textField.text = "";
									RegionSelector["option"+i]._visible = false;
								}
								RegionSelectorScreen.regionTitle._alpha = 0;
								RegionSelector["option"+i].mask._alpha = 0;
							}
						}
						RegionSelector.background._height = 90 + ((boxLength + 1) * 40);
						RegionSelector.background._y = -51.7 - ((5 - boxLength) * 20);
					} else if (RegionSelectorScreen._visible) {
						showRegionsText.text = "$SHOW_REGIONS";
						CloseRegionSelector();
					}
				}
			}
			else if (details.code == modeKey || details.code == modeGamepadKey){
				if (TamrielMap._visible){
					TamrielMap._visible = false;
					TamrielTacticalMap._visible = true;
					ViewSelector_mc._visible = true;
					if(bSeasons && iCurrentTacticalView == 2){
						SeasonHolder_mc._visible = true;
					}
					showRegionsText._visible = false;
					showRegionsButton._visible = false;
					changeModeText.text = "$TRAVEL_VIEW";
					WorldOptionsHolder.textField.text = "";
					DescriptionHolder_mc._visible = false;
					LocStatsHolder_mc._visible = false;
					QuestsOngoingStatsHolder_mc._visible = false;
					QuestsCompletedStatsHolder_mc._visible = false;
				} else {
					TamrielMap._visible = true;
					TamrielTacticalMap._visible = false;
					ViewSelector_mc._visible = false;
					SeasonHolder_mc._visible = false;
					changeModeText.text = "$TACTICAL_VIEW";
					if (TamrielMap[sCurrentProvince].name == undefined){
						WorldOptionsHolder.textField.text = "$TAMRIEL";
					} else {
						WorldOptionsHolder.textField.text = TamrielMap[sCurrentProvince].name;
					}
					BountyHolder_mc._visible = false;
					SurvivalHolder_mc._visible = false;
					AllegianceHolder_mc._visible = false;
					DescriptionHolder_mc._visible = true;
				}
				bHandledInput = true;
			} else if ((details.code == leftKey || details.code == leftGamepadKey) && iCurrentTacticalView >= 1){
				if (TamrielTacticalMap._visible){
					iCurrentTacticalView -= 1;
					GameDelegate.call("PlaySound",["UIMenuPrevNext"]);
					
					ViewSelector_mc.rightButton._alpha = 100;
					SeasonHolder_mc._visible = false;
					if (iCurrentTacticalView == 0){
						ViewSelector_mc.leftButton._alpha = 50;
					}
					aTacticalViews[iCurrentTacticalView].func.call(this);
					ViewSelector_mc.textField.text = aTacticalViews[iCurrentTacticalView].name;
				}
				bHandledInput = true;
			} else if ((details.code == rightKey || details.code == rightGamepadKey) && iCurrentTacticalView <= 1){
				if (TamrielTacticalMap._visible){
					iCurrentTacticalView += 1;
					GameDelegate.call("PlaySound",["UIMenuPrevNext"]);
					
					ViewSelector_mc.leftButton._alpha = 100;
					if (iCurrentTacticalView == 2){
						if (bSeasons){
							SeasonHolder_mc._visible = true;
						}
						ViewSelector_mc.rightButton._alpha = 50;
					}
					aTacticalViews[iCurrentTacticalView].func.call(this);
					ViewSelector_mc.textField.text = aTacticalViews[iCurrentTacticalView].name;
				}
				bHandledInput = true;
			}
		}
		
		return bHandledInput;
	}
	
	function CloseMenu(): Void
	{
		GameDelegate.call("CloseMenu", []);
	}
	
	function CloseRegionSelector(): Void
	{
		RegionSelectorScreen.description.textField.text = "";
		RegionSelectorScreen.regionTitle._alpha = 0;
		
		RegionSelectorScreen.LocStatsHolder_mc._visible = false;
		RegionSelectorScreen.QuestsOngoingStatsHolder_mc._visible = false;
		RegionSelectorScreen.QuestsCompletedStatsHolder_mc._visible = false;
		
		RegionSelectorScreen._visible = false;
		showRegionsText.text = "$SHOW_REGIONS";
	}
	
	function onWorldHover(worldClip: MovieClip): Void
	{
		GameDelegate.call("PlaySound", ["UIMenuFocus"]);
		worldClip._alpha = 90;
		
		DescriptionHolder_mc._visible = true;
		DescriptionHolder_mc.textField.text = worldClip.description;
		WorldOptionsHolder.textField.text = worldClip.name;
		
		showRegionsButton._visible = false;
		showRegionsText._visible = false;
		
		if (worldClip.province.toUpperCase() == worldClip.worlds[0].name) { 
			if(worldClip.worlds[0].stats == undefined){
				GameDelegate.call("GetStats", [worldClip.worlds[0].id, worldClip.worlds[0].plugin]);
			} else {
				SetStats(undefined, undefined, worldClip.worlds[0].stats.discoveredLocs, worldClip.worlds[0].stats.totalLocs, worldClip.worlds[0].stats.questsOngoing, worldClip.worlds[0].stats.questsCompleted);
			}
		} else {
			LocStatsHolder_mc._visible = false;
			QuestsOngoingStatsHolder_mc._visible = false;
			QuestsCompletedStatsHolder_mc._visible = false;
		}
		
		if ((worldClip.province.toUpperCase() != worldClip.worlds[0].name) || (worldClip.province.toUpperCase() == worldClip.worlds[0].name && worldClip.worlds.length > 1)){
			showRegionsButton._visible = true;
			showRegionsText._visible = true;
		}
		
		sCurrentProvince = worldClip.province;
	}
	
	function onWorldOut(worldClip: MovieClip): Void
	{
		worldClip._alpha = 60;
		//worldClip._xscale = 100;
		//worldClip._yscale = 100;
	}
	
	function onWorldPress(worldClip: MovieClip): Void
	{
		if (worldClip.province.toUpperCase() == worldClip.worlds[0].name) {
			GameDelegate.call("ChangeWorld", [worldClip.worlds[0].id, worldClip.worlds[0].plugin]);
		} else {
			worldClip._alpha = 60;
			RegionSelectorScreen._visible = true;
			var boxLength:Number;
			
			for (var i = 0;i < 6; i++){
				if (worldClip.worlds[i] != undefined){
					RegionSelector["option"+i]._visible = true;
					RegionSelector["option"+i].textField.text = worldClip.worlds[i].name;
					RegionSelector["option"+i].id = worldClip.worlds[i].id;
					RegionSelector["option"+i].description = worldClip.worlds[i].description;
					RegionSelector["option"+i].symbol = worldClip.worlds[i].symbol;
					RegionSelector["option"+i].plugin = worldClip.worlds[i].plugin;
					boxLength = i;
				} else {
					RegionSelector["option"+i].textField.text = "";
					RegionSelector["option"+i]._visible = false;
				}
				RegionSelectorScreen.regionTitle._alpha = 0;
				RegionSelector["option"+i].mask._alpha = 0;
			}
			RegionSelector.background._height = 90 + ((boxLength + 1) * 40);
			RegionSelector.background._y = -51.7 - ((5 - boxLength) * 20);
		}
	}
	
	function onTerritoryHover(territoryClip: MovieClip): Void {
		GameDelegate.call("PlaySound", ["UIMenuFocus"]);
		territoryClip._alpha = 90;
		
		if(territoryClip.faction == undefined){
			territoryClip.faction = "-"
		}
		
		AllegianceHolder_mc._visible = true;
		BountyHolder_mc._visible = true;
		SurvivalHolder_mc._visible = true;
		AllegianceHolder_mc.infoText.text = territoryClip.faction;
		BountyHolder_mc.statsText.text = territoryClip.crimeGold;
		SurvivalHolder_mc.statsText.text = territoryClip.survivalName;
		WorldOptionsHolder.textField.text = territoryClip.name;
	}
	
	function onTerritoryOut(territoryClip: MovieClip): Void
	{
		territoryClip._alpha = 75;
	}
	
	function onOptionHover(option: MovieClip): Void
	{
		GameDelegate.call("PlaySound", ["UIMenuFocus"]);
		option.textField._alpha = 90;
		RegionSelectorScreen.description.textField.text = option.description;
		RegionSelectorScreen.regionTitle.symbol.gotoAndStop(option.symbol);
		RegionSelectorScreen.regionTitle._alpha = 100;
		if(option.stats == undefined){
			GameDelegate.call("GetStats", [option.id, option.plugin]);
		} else {
			SetStats(undefined, undefined, option.stats.discoveredLocs, option.stats.totalLocs, option.stats.questsOngoing, option.stats.questsCompleted);
		}
	}
	
	function onOptionOut(option: MovieClip): Void
	{
		option.textField._alpha = 60;
	}
	
	function onOptionPress(option: MovieClip): Void
	{
		GameDelegate.call("ChangeWorld", [option.id, option.plugin]);
	}
	
	/*function moveList(moveLeft){
		GameDelegate.call("PlaySound",["UIMenuPrevNext"]);
		if(moveLeft){
			iLeftmostOption -= 1;
		} else {
			iLeftmostOption += 1;
		}
		WorldOptionsHolder.leftPane.world = worldsList[iLeftmostOption];
		WorldOptionsHolder.leftPane.textField.text = worldsList[iLeftmostOption].name.toUpperCase();
		WorldOptionsHolder.leftPane.symbol.gotoAndStop(worldsList[iLeftmostOption].symbol);
		WorldOptionsHolder.rightPane.world = worldsList[iLeftmostOption + 1];
		WorldOptionsHolder.rightPane.textField.text = worldsList[iLeftmostOption + 1].name.toUpperCase();
		WorldOptionsHolder.rightPane.symbol.gotoAndStop(worldsList[iLeftmostOption + 1].symbol);
		DescriptionHolder_mc.textField.text = "";
	}*/
	
	function SetWorldOptions(worldOptions){
		var province:String;
		for (province in TamrielMap){
			var worldsList = [];
			for (var i in worldOptions){
				var option = worldOptions[i];
				if (option.mapRegion.toUpperCase() == province.toUpperCase()){
					if (option.worldName.toUpperCase() != province.toUpperCase()){
						worldsList.push({name: option.worldName.toUpperCase(), id: option.worldID, plugin: option.plugin, description: option.description, symbol: option.symbol});
					} else {
						worldsList.unshift({name: option.worldName.toUpperCase(), id: option.worldID, plugin: option.plugin});
					}
				}
			}
			
			TamrielMap[province].worlds = worldsList;
			
			if (worldsList.length == 0){
				TamrielMap[province]._alpha = 20;
				TamrielMap[province].onRollOver = function()
				{
					this._alpha = 30;
					_parent._parent.DescriptionHolder_mc._visible = true;
					_parent._parent.DescriptionHolder_mc.textField.text = this.description;
					_parent._parent.WorldOptionsHolder.textField.text = this.name;
					_parent._parent.LocStatsHolder_mc._visible = false;
					_parent._parent.QuestsOngoingStatsHolder_mc._visible = false;
					_parent._parent.QuestsCompletedStatsHolder_mc._visible = false;
					_parent._parent.showRegionsButton._visible = false;
					_parent._parent.showRegionsText._visible = false;
					_parent._parent.sCurrentProvince = this.province;
				}
				TamrielMap[province].onRollOut = function()
				{
					this._alpha = 20;
				}
				TamrielMap[province].onMouseDown = function()
				{
					if (Mouse.getTopMostEntity() == this){
						return;
					}
				}
			}
		}
	}
	
	function SetTacticalRegions(regionOptions:Array):Void
	{
		var option:Object;
		var province:String;
		var territory:String;
		var region:Object;
		var factionColor:Number;
		var survivalColor:Number;
		
		for (province in TamrielTacticalMap)
		{
			for (territory in TamrielTacticalMap[province])
			{
				region = TamrielTacticalMap[province][territory];
	
				region.faction = "-";
				region.factionColor = 0xFDFBFE;
				region.crimeGold = 0;
				region.crimeType = "";
				region.survivalName = "-";
				region.survivalColor = 0x525252;
	
				new Color(region).setRGB(region.factionColor);
			}
		}
	
		for (var i:Number = 0; i < regionOptions.length; i++)
		{
			option = regionOptions[i];
	
			province = option.provinceName;
			territory = option.regionName;
	
			if (TamrielTacticalMap[province] == undefined)
			{
				continue;
			}
	
			region = TamrielTacticalMap[province][territory];
	
			if (region == undefined)
			{
				continue;
			}
	
			if (option.factionName != undefined)
			{
				region.faction = option.factionName;
			}
	
			if (option.factionColor != undefined)
			{
				if (typeof(option.factionColor) == "string")
				{
					factionColor = parseInt(option.factionColor.substr(1), 16);
				}
				else
				{
					factionColor = Number(option.factionColor);
				}
	
				if (!isNaN(factionColor))
				{
					region.factionColor = factionColor;
				}
			}
	
			if (option.crime != undefined)
			{
				region.crimeGold = option.crime;
			}
	
			if (option.crimeType != undefined)
			{
				region.crimeType = option.crimeType;
			}
			
			if (option.survivalName != undefined)
			{
				region.survivalName = option.survivalName;
			}
			
			if (option.survivalColor != undefined)
			{
				if (typeof(option.survivalColor) == "string")
				{
					survivalColor = parseInt(option.survivalColor.substr(1), 16);
				}
				else
				{
					survivalColor = Number(option.survivalColor);
				}
	
				if (!isNaN(survivalColor))
				{
					region.survivalColor = survivalColor;
				}
			}
	
			new Color(region).setRGB(region.factionColor);
		}
	}
	
	function ShowAlliances(){
		for (var province in TamrielTacticalMap)
		{
			for (var territory in TamrielTacticalMap[province])
			{
				var region = TamrielTacticalMap[province][territory];
		
				new Color(region).setRGB(region.factionColor);
			}
		}
	}
	
	function ShowCrime(){
		for (var province in TamrielTacticalMap)
		{
			for (var territory in TamrielTacticalMap[province])
			{
				var region = TamrielTacticalMap[province][territory];
		
				if(region.crimeType == "attack"){
					new Color(region).setRGB(0xFF6A52);
				} else if (region.crimeType == "arrest") {
					new Color(region).setRGB(0xFFE769);
				} else if (region.crimeType == "petty") {
					new Color(region).setRGB(0xFDFBFE);
				} else {
					new Color(region).setRGB(0x878787);
				}
			}
		}
	}
	
	function ShowSurvival(){
		for (var province in TamrielTacticalMap)
		{
			for (var territory in TamrielTacticalMap[province])
			{
				var region = TamrielTacticalMap[province][territory];
		
				new Color(region).setRGB(region.survivalColor);
			}
		}
	}
	
	function SetStats(worldId, plugin, discoveredLocs, totalLocs, questsOngoing, questsCompleted){
		if (RegionSelectorScreen._visible == true){
			RegionSelectorScreen.LocStatsHolder_mc._visible = true;
			RegionSelectorScreen.QuestsOngoingStatsHolder_mc._visible = true;
			RegionSelectorScreen.QuestsCompletedStatsHolder_mc._visible = true;
			
			RegionSelectorScreen.LocStatsHolder_mc.statsText.text = discoveredLocs+" / "+totalLocs;
			RegionSelectorScreen.QuestsOngoingStatsHolder_mc.statsText.text = questsOngoing;
			RegionSelectorScreen.QuestsCompletedStatsHolder_mc.statsText.text = questsCompleted;
			
		} else {
			LocStatsHolder_mc._visible = true;
			QuestsOngoingStatsHolder_mc._visible = true;
			QuestsCompletedStatsHolder_mc._visible = true;
			
			LocStatsHolder_mc.statsText.text = discoveredLocs+" / "+totalLocs;
			QuestsOngoingStatsHolder_mc.statsText.text = questsOngoing;
			QuestsCompletedStatsHolder_mc.statsText.text = questsCompleted;
		}
		
		if (worldId != undefined || plugin != undefined){
			var cachingWorld = FindWorld(worldId, plugin);
			cachingWorld.stats = {discoveredLocs: discoveredLocs, totalLocs: totalLocs, questsOngoing: questsOngoing, questsCompleted: questsCompleted};
		}
	}
	
	function SetSeasonText(isEnabled, season){
		bSeasons = isEnabled;
		
		var seasonName:String;
		
		switch (season){
			case 1:
				seasonName = "$WINTER";
				break;
			case 2:
				seasonName = "$SPRING";
				break;
			case 3:
				seasonName = "$SUMMER";
				break;
			case 4:
				seasonName = "$AUTUMN";
				break;
			default:
				seasonName = "$SUMMER";
				break;
		}
		SeasonHolder_mc.infoText.text = seasonName;
	}
	
	function SetGamepad(gamepad, menuHotkeyNum, menuGamepadHotkeyNum, regionsKeyNum, regionsGamepadKeyNum, modeKeyNum, modeGamepadKeyNum, leftKeyNum, leftGamepadKeyNum, rightKeyNum, rightGamepadKeyNum)
	{
		menuHotkey = DxScanToWindows.dxScanCodeToVK(menuHotkeyNum);
		menuGamepadHotkey = DxScanToWindows.dxScanCodeToVK(menuGamepadHotkeyNum);
		regionsKey = DxScanToWindows.dxScanCodeToVK(regionsKeyNum);
		regionsGamepadKey = DxScanToWindows.dxScanCodeToVK(regionsGamepadKeyNum);
		modeKey = DxScanToWindows.dxScanCodeToVK(modeKeyNum);
		modeGamepadKey = DxScanToWindows.dxScanCodeToVK(modeGamepadKeyNum);
		leftKey = DxScanToWindows.dxScanCodeToVK(leftKeyNum);
		leftGamepadKey = DxScanToWindows.dxScanCodeToVK(leftGamepadKeyNum);
		rightKey = DxScanToWindows.dxScanCodeToVK(rightKeyNum);
		rightGamepadKey = DxScanToWindows.dxScanCodeToVK(rightGamepadKeyNum);

		bGamepad = gamepad;
		if (!bGamepad){
			showRegionsButton.gotoAndStop(regionsKeyNum);
			changeModeButton.gotoAndStop(modeKeyNum);
			ViewSelector_mc.leftButton.gotoAndStop(leftKeyNum);
			ViewSelector_mc.rightButton.gotoAndStop(rightKeyNum);
		}else{
			showRegionsButton.gotoAndStop(regionsGamepadKeyNum);
			changeModeButton.gotoAndStop(modeGamepadKeyNum);
			ViewSelector_mc.leftButton.gotoAndStop(leftGamepadKeyNum);
			ViewSelector_mc.rightButton.gotoAndStop(rightGamepadKeyNum);
		}
	}
	
	function SetPlatform(a_platform:Number, a_bPS3Switch:Boolean):Void
	{
		var isGamepad = _platform != 0;

		if (a_platform == 0)
		{
			_deleteControls = {keyCode:45};// X
			_defaultControls = {keyCode:20};// T
			_kinectControls = {keyCode:37};// K
			_acceptControls = {keyCode:28};// Enter
			_cancelControls = {keyCode:15};// Tab
		}
		else
		{
			_deleteControls = {keyCode:278};// 360_X
			_defaultControls = {keyCode:279};// 360_Y
			_kinectControls = {keyCode:275};// 360_RB
			_acceptControls = {keyCode:276};// 360_A
			_cancelControls = {keyCode:277};// 360_B
		}

		_acceptButton.addEventListener("click",this,"onAcceptMousePress");
		_cancelButton.addEventListener("click",this,"onCancelMousePress");

		_platform = a_platform;
	}
	
	function FindWorld(
		worldID:String,
		plugin:String):Object
	{
		var province:String;
	
		for (province in TamrielMap)
		{
			var worlds:Array =
				TamrielMap[province].worlds;
	
			if (worlds == undefined)
			{
				continue;
			}
	
			for (var i:Number = 0;
				 i < worlds.length;
				 i++)
			{
				var world:Object = worlds[i];
	
				if (world.id == worldID &&
					world.plugin == plugin)
				{
					return world;
				}
			}
		}
	
		return undefined;
	}
}
