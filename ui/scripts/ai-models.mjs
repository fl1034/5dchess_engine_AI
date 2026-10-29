// Standard text-token prices per million tokens, checked against the model docs.
export const MODELS = [
    {id:'gpt-6-luna',label:'GPT-6 Luna',input:0.1,cached:0.01,output:0.5},
    {id:'gpt-6.1-terra',label:'GPT-6.1 Terra',input:1,cached:0.05,output:5},
    {id:'gpt-6.1-sol',label:'GPT-6.1 Sol',input:2,cached:0.1,output:10},
    {id:'gpt-5-mini',label:'GPT-5 mini',input:0.25,cached:0.025,output:2},
];

export const findModel = id => MODELS.find(model => model.id === id);

export function usageCost(modelId, usage) {
    const model = findModel(modelId);
    if (!model) throw Error('Unknown model for usage estimate.');
    const input = usage.prompt_tokens ?? 0;
    const cached = Math.min(input,usage.prompt_tokens_details?.cached_tokens ?? 0);
    return ((input-cached)*model.input + cached*model.cached + (usage.completion_tokens ?? 0)*model.output)/1000000;
}
