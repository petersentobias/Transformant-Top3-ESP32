// Cloudflare Worker: secure Business Central Resource Ledger Top 3 endpoint.
// Configure secrets with wrangler secret put BC_TENANT_ID, BC_CLIENT_ID,
// BC_CLIENT_SECRET, and DEVICE_API_KEY. Never commit actual values.
const base = "https://api.businesscentral.dynamics.com/v2.0/Production/ODataV4/Company('Transformant%20AB')/Res_LedgerEntries";
const alias = { CALAND: "Calle Andersson", JOASOD: "Joakim Södergård" };
const json = (body, status = 200) => new Response(JSON.stringify(body), { status, headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" } });
function swedishDay() { return new Intl.DateTimeFormat("en-CA", { timeZone: "Europe/Stockholm", year: "numeric", month: "2-digit", day: "2-digit" }).format(new Date()); }
async function token(env) {
 const p = new URLSearchParams({ grant_type:"client_credentials",client_id:env.BC_CLIENT_ID,client_secret:env.BC_CLIENT_SECRET,scope:"https://api.businesscentral.dynamics.com/.default" });
 const r = await fetch("https://login.microsoftonline.com/"+encodeURIComponent(env.BC_TENANT_ID)+"/oauth2/v2.0/token",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:p});
 if(!r.ok) throw new Error("OAuth "+r.status);
 const data=await r.json();if(!data.access_token)throw new Error("Missing access token");return data.access_token;
}
async function compute(env) {
 const end=swedishDay(),start=end.slice(0,7)+"-01";
 const filter="Posting_Date ge "+start+" and Posting_Date le "+end+" and Entry_Type eq 'Usage'";
 const url=new URL(base);url.searchParams.set("$filter",filter);
 url.searchParams.set("$select","Entry_No,Resource_No,Resource_Name,Job_No,Posting_Date,Quantity,Unit_Price");
 let next=url.toString(),pages=0,rows=0;const totals=new Map(),bearer=await token(env);
 while(next){
   if(++pages>500)throw new Error("Pagination limit reached");
   const u=new URL(next);
   if(u.protocol!=="https:"||u.hostname!=="api.businesscentral.dynamics.com")throw new Error("Unsafe nextLink");
   const r=await fetch(u.toString(),{headers:{Authorization:"Bearer "+bearer,Accept:"application/json"}});
   if(!r.ok)throw new Error("BC HTTP "+r.status);
   const page=await r.json();if(!Array.isArray(page.value))throw new Error("Invalid BC response");
   for(const item of page.value){
     rows++;if(String(item.Job_No||"").toUpperCase()==="TFM")continue;
     const price=Number(item.Unit_Price),hours=Number(item.Quantity);
     if(!Number.isFinite(price)||price<=0||!Number.isFinite(hours)||hours<=0)continue;
     const code=String(item.Resource_No||"").trim().toUpperCase();
     const name=alias[code]||String(item.Resource_Name||item.Resource_No||"").trim();
     if(!name)continue;
     const key=code||name;const t=totals.get(key)||{name,amount:0,hours:0};
     t.amount+=hours*price;t.hours+=hours;totals.set(key,t);
   }
   next=page["@odata.nextLink"]||null;
 }
 const top3=[...totals.values()].sort((a,b)=>b.amount-a.amount).slice(0,3).map((t,i)=>({rank:i+1,name:t.name,amount_sek:Math.round(t.amount),hours:Math.round(t.hours*100)/100}));
 return {period_start:start,period_end:end,updated_at:new Date().toISOString(),top3,source_rows:rows};
}
let cached=null;
export default {
 async fetch(request,env){
   const u=new URL(request.url);
   if(u.pathname!=="/api/top3")return json({error:"Not found"},404);
   if(request.method!=="GET")return json({error:"Method not allowed"},405);
   const provided=request.headers.get("X-Device-Key")||"";
   if(!env.DEVICE_API_KEY||provided!==env.DEVICE_API_KEY)return json({error:"Unauthorized"},401);
   try{
     const today=swedishDay();
     if(cached&&cached.day===today&&Date.now()-cached.at<300000)return json(cached.data);
     const data=await compute(env);cached={day:today,at:Date.now(),data};return json(data);
   }catch(e){
     console.error("Top3 retrieval failed:",String(e.message||e));
     if(cached)return json({...cached.data,stale:true});
     return json({error:"Upstream unavailable"},502);
   }
 }
};
